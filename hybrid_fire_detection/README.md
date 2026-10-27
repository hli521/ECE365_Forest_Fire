# Device-Side Hybrid Fire Detection

## Setup and Usage

### 1. Upload the Firmware

Use PlatformIO to build and upload the firmware to the corresponding ESP32 boards.

**Sensor Device:**

```bash
pio run -e heltec_wifi_lora_32_V3 -t upload --upload-port /dev/cu.usbserial-XXXX
```

**Server (LoRa Receiver):**

```bash
pio run -e heltec_lora_server -t upload --upload-port /dev/cu.usbserial-YYYY
```

Replace the serial port names with the actual ports connected to your devices.

### 2. Run the Dashboard

Install the required Python dependency:

```bash
pip install pyserial
```

Start the dashboard using the server's serial port:

```bash
python src/dashboard.py /dev/cu.usbserial-YYYY
```

Open the dashboard in your browser:

http://localhost:8000

### 3. Run Tests (Optional)

To test the hybrid detection algorithm without ESP32 hardware:

```bash
g++ -std=c++17 -Iinclude test_hybrid.cpp -o test_hybrid
./test_hybrid
```

Run these commands from the project root, with `test_hybrid.cpp` located there.

## Notes

- Fire detection runs entirely on the **Device**.
- The **Server** receives sensor readings and fire detection results through LoRa.
- The existing FFS5 packet format remains unchanged.
- Detection thresholds are experimental and require calibration before real-world deployment.