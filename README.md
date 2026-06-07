# Zigbee Air Quality Monitor

This project uses an **ESP32-H2-Zero-M** and a **ZPHS01C** air quality sensor module to build a native Zigbee 3.0 environmental monitor. The ESP32-H2 features a built-in 802.15.4 radio, allowing it to communicate directly with Zigbee coordinators (like Zigbee2MQTT or ZHA).

## Sensor Overview

The [Winsen ZPHS01C](https://www.winsen-sensor.com/sensors/air-quality-sensor/zphs01c.html) is a "multi-in-one" air quality module. Note that this project uses the VOC version, as detailed in the [manual](docs/winsen%20ZPHS01C%20manual.pdf).

![ZPHS01C-VOC](https://www.winsen-sensor.com/d/pic/zphs01c-voc.jpg)

### Onboard Sensors

*   **Temperature & Humidity (Pigtail Extension):** The tiny green board connected via the colored cable is the Temperature and Humidity Sensor. It is placed on a short extension cable specifically to isolate it from the main board. The other sensors on this module (especially the laser dust sensor, which has a small spinning fan, and the infrared $CO_2$ sensor) generate a slight amount of heat when they are powered on. If the temperature/humidity sensor were soldered directly onto the main green circuit board, it would absorb that heat and give you artificially high temperature readings. Hanging it off the edge ensures it is measuring the actual ambient air in the room, not the heat of the electronics.
*   **MH-Z19E (Top Left):** The rectangular metal box with the white membrane. It measures $CO_2$.
*   **ZH06 (Bottom):** The large silver box with the blue sticker. This is a laser particle sensor that measures dust, smoke, and aerosols (PM1.0, PM2.5, and PM10).
*   **Cylindrical Sensor (Top Right):** This is an electrochemical or semiconductor sensor used to measure VOCs (Volatile Organic Compounds).

## Hardware Wiring

The ZPHS01C-Z communicates via UART (Serial). The logic level is 3.3V (which is safe for the ESP32), but the module requires a **5V power supply**.

### The J1 Connector (TRVG)
The sensor module has a small 4-pin JST connector labeled **J1** with the pins marked **T R V G**. This connector is typically a **JST GH (1.25mm pitch)** or **JST ZH (1.5mm pitch)** female receptacle. 

Since it uses a micro-connector, standard Dupont jumper wires will not fit. You will need a pre-crimped 4-pin JST cable (1.25mm or 1.5mm pitch) to connect it cleanly.

### Wiring Map

| ZPHS01C-Z Pin (TRVG) | ESP32-H2-Zero-M Pin | Description |
| :--- | :--- | :--- |
| **T** (TX) | Pin `4` (Acts as RX) | Transmit data to ESP32 |
| **R** (RX) | Pin `5` (Acts as TX) | Receive data from ESP32 |
| **V** (VCC) | `5V` (or `VBUS`) | **Must be 5V!** (Powers internal heaters) |
| **G** (GND) | `GND` | Common Ground |

## Software Setup

You will need `arduino-cli` and the ESP32 Core **v3.0.0 or higher** for native Zigbee support.

1. Install `arduino-cli` following the [official instructions](https://arduino.github.io/arduino-cli/latest/installation/).
2. Add the ESP32 board manager URL and install the core (v3.0.0+):
   ```bash
   arduino-cli config init
   arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core update-index
   arduino-cli core install esp32:esp32
   ```

## Usage

1. Navigate to the project directory.
2. Compile, upload to the board, and monitor the logs all in one command (replace `/dev/ttyACM0` with your actual port, e.g., `/dev/ttyUSB0` or `COM3`). Note the `CDCOnBoot=cdc` flag, which is required for USB serial output:
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32h2:PartitionScheme=zigbee,ZigbeeMode=ed,CDCOnBoot=cdc ZPHS01C && arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32h2:PartitionScheme=zigbee,ZigbeeMode=ed,CDCOnBoot=cdc ZPHS01C && arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
   ```

**Permission Denied Error?** 
If you get a `Permission denied: '/dev/ttyACM0'` error on Linux, you can fix it temporarily by granting read/write permissions to the serial port:
```bash
sudo chmod a+rw /dev/ttyACM0
```
*(For a permanent fix, add your user to the dialout group: `sudo usermod -a -G dialout $USER` and log out/in).*

### Features Supported
The monitor parses the active stream from the ZPHS01C and exposes the following Zigbee Endpoints to Home Assistant/Zigbee2MQTT:
- **Temperature & Humidity**
- **Carbon Dioxide (CO2)**
- **PM2.5**
- **VOC** (Exposed as Atmospheric Pressure, as VOC is not natively supported in the base Zigbee library)
- **Identify** (Flashes the onboard WS2812 RGB LED green)

**Next Steps:**
1. Once powered on, the ESP32 will automatically enter pairing mode.
2. Open your Zigbee Coordinator (e.g., Home Assistant) and click "Add Device" to pair it.

> Note: The ZPHS01C has a pre-heat time of about 3 minutes when first powered on. During this time, readings (especially VOC/CH2O) might be unstable.

## Troubleshooting / Factory Reset

If you remove the device from your Zigbee Coordinator (e.g., Home Assistant), the ESP32 still retains the network credentials in its non-volatile memory (NVS) and will automatically rejoin the network on the next boot. 

To **factory reset** the device and clear its Zigbee memory, you need to erase the flash using `esptool` (which is bundled with the Arduino ESP32 core). 

Run the following command to completely erase the flash memory:
```bash
~/.arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool --port /dev/ttyACM0 erase_flash
```

After erasing the flash, re-upload the sketch. The device will boot with a clean memory and will go back into pairing mode.
