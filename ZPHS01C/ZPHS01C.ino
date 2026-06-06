#include <Arduino.h>
#include <Zigbee.h>

// UART pins for ESP32-H2 (Connected to T and R on the ZPHS01C)
#define RX_PIN 4  // Connect to 'T' (TX) on ZPHS01C
#define TX_PIN 5  // Connect to 'R' (RX) on ZPHS01C

// Define Zigbee Endpoints for the sensors
#define TEMP_ENDPOINT 10
#define CO2_ENDPOINT 11
#define PM25_ENDPOINT 12
#define VOC_ENDPOINT 13

// Initialize Zigbee Sensor classes
ZigbeeTempSensor tempSensor(TEMP_ENDPOINT);
ZigbeeCarbonDioxideSensor co2Sensor(CO2_ENDPOINT);
ZigbeePM25Sensor pm25Sensor(PM25_ENDPOINT);
ZigbeePressureSensor vocSensor(VOC_ENDPOINT); // VOC not natively in Zigbee, repurpose pressure cluster

// Rate-limiting updates are handled by the Zigbee library

uint8_t rxBuffer[64];
int rxIndex = 0;
unsigned long lastPoll = 0;
unsigned long lastDataReceived = 0;

// Internal state for the identify effect
bool isIdentifying = false;
unsigned long identifyEndTime = 0;

// Callback for the Zigbee Identify cluster
void onIdentifyCommand(uint16_t time) {
  Serial.printf("Identify command received. Blinking for %d seconds\n", time);
  if (time > 0) {
    isIdentifying = true;
    identifyEndTime = millis() + (time * 1000);
  } else {
    isIdentifying = false;
  }
}

void setup() {
  // Debug Serial
  Serial.begin(115200);

  // Give some time to open the serial monitor before things speed away.
  delay(3000);
  Serial.println("\n\n--- ESP32 RUNNING ---");
  
  // Sensor Serial (ZPHS01C default is 9600 baud, 8N1)
  Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

  // The new ZigbeeTempSensor class handles both temperature and humidity
  tempSensor.addHumiditySensor();

  // Set minimum and maximum temperature to avoid defaulting to 0
  tempSensor.setMinMaxValue(-10.0, 80.0);

  // Setup the built-in LED (GPIO8 on ESP32-H2-DevKitM) for the identify effect
#ifdef RGB_BUILTIN
  neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Turn off initially
#else
  pinMode(8, OUTPUT);
  digitalWrite(8, LOW);
#endif

  // Set the manufacturer and model name so Home Assistant recognizes it properly
  tempSensor.setManufacturerAndModel("Winsen", "ZPHS01C Air Monitor");
  co2Sensor.setManufacturerAndModel("Winsen", "ZPHS01C Air Monitor");
  pm25Sensor.setManufacturerAndModel("Winsen", "ZPHS01C Air Monitor");
  vocSensor.setManufacturerAndModel("Winsen", "ZPHS01C Air Monitor");

  // Attach the identify callback to react to "Identify" commands
  tempSensor.onIdentify(onIdentifyCommand);
  co2Sensor.onIdentify(onIdentifyCommand);
  pm25Sensor.onIdentify(onIdentifyCommand);
  vocSensor.onIdentify(onIdentifyCommand);

  // Add the sensor endpoint to the Zigbee network
  Zigbee.addEndpoint(&tempSensor);
  Zigbee.addEndpoint(&co2Sensor);
  Zigbee.addEndpoint(&pm25Sensor);
  Zigbee.addEndpoint(&vocSensor);
  
  // Initialize Zigbee as an End Device AFTER adding endpoints
  if (!Zigbee.begin(ZIGBEE_END_DEVICE)) {
    Serial.println("Zigbee failed to start!");
    delay(1000);
    ESP.restart();
  }
  
  Serial.println("Connecting to Zigbee network in the background...");

  // Set up reporting MUST be called AFTER Zigbee.begin()
  // Set up reporting: send report if temp changes by 0.1C (delta=10) and humidity changes by 1.00% (delta=100)
  tempSensor.setReporting(5, 60, 10);
  tempSensor.setHumidityReporting(5, 60, 100);
  co2Sensor.setReporting(5, 60, 10); // Report CO2 on 10ppm change
  pm25Sensor.setReporting(5, 60, 1); // Report PM2.5 on 1 ug/m3 change
  vocSensor.setReporting(5, 60, 1);  // Report VOC on 1 unit change
  
  Serial.println("Zigbee Air Quality Monitor Starting...");
}

void loop() {
  // poll command every 5 seconds if no data is being received
  if (millis() - lastPoll > 5000 && millis() - lastDataReceived > 5000) {
    lastPoll = millis();
    const uint8_t pollCmd[] = {0x11, 0x02, 0x01, 0x00, 0xEC};
    Serial1.write(pollCmd, sizeof(pollCmd));
    Serial.println("Sent poll command to sensor...");
  }

  // Read data
  while (Serial1.available() > 0) {
    if (rxIndex >= sizeof(rxBuffer)) {
      // Buffer full, prevent overflow by dropping the oldest byte
      memmove(rxBuffer, rxBuffer + 1, sizeof(rxBuffer) - 1);
      rxIndex--;
    }
    rxBuffer[rxIndex++] = Serial1.read();
  }

  // Parse data
  while (rxIndex >= 4) { // Minimum length to check HEAD and LEN
    if (rxBuffer[0] != 0x16) {
      // Shift buffer to find 0x16
      memmove(rxBuffer, rxBuffer + 1, rxIndex - 1);
      rxIndex--;
      continue;
    }

    int expectedLen = rxBuffer[1] + 3;
    if (expectedLen > sizeof(rxBuffer)) {
      // Invalid length byte, discard the header to avoid an infinite stall
      memmove(rxBuffer, rxBuffer + 1, rxIndex - 1);
      rxIndex--;
      continue;
    }

    if (rxIndex >= expectedLen) {
      // Calculate checksum
      uint8_t sum = 0;
      for (int i = 0; i < expectedLen - 1; i++) {
        sum += rxBuffer[i];
      }
      uint8_t cs = (~sum) + 1;

      if (cs == rxBuffer[expectedLen - 1]) {
        // Valid packet!
        lastDataReceived = millis();
        uint8_t cmd = rxBuffer[2];
        
        if (cmd == 0x01 || cmd == 0x02) {
          int co2 = (rxBuffer[3] << 8) | rxBuffer[4];
          int voc_ch2o = (rxBuffer[5] << 8) | rxBuffer[6];
          int hum_raw = (rxBuffer[7] << 8) | rxBuffer[8];
          int temp_raw = (rxBuffer[9] << 8) | rxBuffer[10];
          int pm25 = (rxBuffer[11] << 8) | rxBuffer[12];
          
          float humidity = hum_raw / 10.0;
          float temperature = (temp_raw - 500) / 10.0;
          
          Serial.printf("CO2: %d ppm, VOC/CH2O: %d, Hum: %.1f %%, Temp: %.1f C, PM2.5: %d ug/m3\n", 
                        co2, voc_ch2o, humidity, temperature, pm25);
          
          // Update Zigbee clusters
          tempSensor.setTemperature(temperature);
          tempSensor.setHumidity(humidity);
          co2Sensor.setCarbonDioxide((float)co2);
          pm25Sensor.setPM25((float)pm25);
          vocSensor.setPressure((int16_t)voc_ch2o);
        }
      } else {
        Serial.printf("Checksum failed! Expected %02X, got %02X\n", cs, rxBuffer[expectedLen - 1]);
      }
      
      // Remove parsed packet from buffer
      memmove(rxBuffer, rxBuffer + expectedLen, rxIndex - expectedLen);
      rxIndex -= expectedLen;
    } else {
      // Not enough data yet
      break;
    }
  }

  // Handle the Identify effect (Blink the LED)
  if (isIdentifying) {
    if (millis() > identifyEndTime) {
      isIdentifying = false;
#ifdef RGB_BUILTIN
      neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Turn off LED
#else
      digitalWrite(8, LOW);
#endif
    } else {
      // Blink LED every 500ms
      if ((millis() / 500) % 2 == 0) {
#ifdef RGB_BUILTIN
        // WS2812 on ESP32-H2 often uses GRB order. 
        // neopixelWrite(pin, r, g, b) shifts out r, then g, then b.
        // So passing (64, 0, 0) sends 64 to the first channel (Green).
        neopixelWrite(RGB_BUILTIN, 64, 0, 0); // Green color
#else
        digitalWrite(8, HIGH);
#endif
      } else {
#ifdef RGB_BUILTIN
        neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Turn off LED
#else
        digitalWrite(8, LOW);
#endif
      }
    }
  }

  // Yield CPU to FreeRTOS
  delay(10);
}
