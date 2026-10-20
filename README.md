# Early-Warning Forest Fire Sensors

Two Heltec WiFi LoRa 32 V3 boards exchange sensor readings over LoRa:

| Role | Source | PlatformIO environment |
| --- | --- | --- |
| Device: reads sensors and transmits readings | `src/device.cpp` | `heltec_wifi_lora_32_V3` |
| Server: receives readings and shows them on OLED and USB serial | `src/server.cpp` | `heltec_lora_server` |

The server is firmware on the second Heltec board. No desktop server, Wi-Fi connection, or cloud service is required. A computer is used to build, upload, and optionally view serial output.

## Requirements

- Two **Heltec WiFi LoRa 32 V3** boards, suitable LoRa antennas, and USB data cables. Attach antennas before powering the radios.
- Any combination of Grid-EYE thermal sensor, PMSA003I I2C particulate sensor, DHT11, HW-691 (MLX90614), and SVM41 for the device board.
- VS Code with PlatformIO IDE, or PlatformIO Core installed separately.
- Internet access for the first build to download the platform, toolchain, and libraries listed in `platformio.ini`.

Open this project folder in VS Code and open a **PlatformIO terminal**. Run all commands below from the folder containing `platformio.ini`. If `pio` is not found in a regular macOS terminal, use the PlatformIO terminal or, for the default installation, `~/.platformio/penv/bin/pio` instead.

## Wire the device

Disconnect power before changing connections. The server needs no external sensors.

| Sensor connection | Device board connection |
| --- | --- |
| Grid-EYE SDA | GPIO41 |
| Grid-EYE SCL | GPIO42 |
| PMSA003I SDA | GPIO41, shared with Grid-EYE |
| PMSA003I SCL | GPIO42, shared with Grid-EYE |
| DHT11 DATA | GPIO4 or GPIO3 (automatically detected) |
| HW-691 SDA | GPIO41, shared I2C bus |
| HW-691 SCL | GPIO42, shared I2C bus |
| SVM41 SDA / SCL | GPIO41 / GPIO42 |
| SVM41 VDD | 3.3V |
| SVM41 GND and SEL | GND (SEL selects I2C mode) |
| All sensor grounds | Heltec GND, including any external power supply ground |

Use the power supply specified for your exact sensor or breakout board; the table above covers signal wiring. The [bare PMSA003I module](https://www.adafruit.com/product/4505) needs 5V power and 3.3V I2C logic with external pull-ups. Breakout boards can include power conversion and pull-ups; check their documentation before wiring. Keep signals connected to the Heltec at 3.3V logic levels.

For a bare DHT sensor, use a 4.7–10 kΩ pull-up from DATA to 3.3V; a sensor module may already include one. Check the actual module's pin labels rather than assuming its pin order. See the [DHT wiring guide](https://learn.adafruit.com/dht?view=all).

The device uses the second I2C controller at 100 kHz for GPIO41/42. The onboard OLED uses GPIO17/18 on the other controller. Expected sensor addresses are:

- Grid-EYE: `0x69` by default, or `0x68` with its address jumper closed; the code tries both.
- PMSA003I: `0x12`. Confirm that the sensor is the I2C model.
- SVM41: `0x6A`, with SEL grounded for I2C.
- HW-691 (MLX90614): `0x5A` by default; configurable with `MLX_ADDRESS` in `src/device.cpp`.
- DHT11: uses GPIO4 or GPIO3 directly and does not appear in an I2C scan.

The device prefers GPIO4 at startup, then keeps using the last working DHT pin. If a reading fails, it tries the other pin. If neither pin succeeds during startup, reset after fixing the connection. Connect one DHT DATA wire to either pin.

The code selects `DHT11` in `src/device.cpp`. If your sensor is a DHT22, change `DHT_TYPE` to `DHT22` before building. The current display/log labels still say DHT11.

## Automatic sensor detection and packets

At startup, the device probes Grid-EYE at `0x69`/`0x68`, initializes PMSA003I at `0x12`, MLX90614 at `0x5A`, and SVM41 at `0x6A`, and tries up to three DHT detection rounds on GPIO4 and GPIO3, two seconds apart, after the initial three-second startup wait. Only detected sensors are read during normal operation and included in LoRa packets. I2C address probes assume the expected sensor is wired at that address; they do not identify arbitrary sensor models.

The `FFS5` packet contains magic (4 bytes), sequence (4), sensor presence (1), reading validity (1), the detected sensors' data, and fire level/reasons (2). Both masks use bit 0 for Grid-EYE, bit 1 for PMSA003I, bit 2 for DHT11, bit 3 for MLX90614, and bit 4 for SVM41. Data appears in that order: thermal pixels (128 bytes), particulate values (6), DHT temperature/humidity (4), MLX object/ambient temperatures (4), and SVM41 temperature/RH/VOC Index/NOx Index (8). SVM41 fields are signed 16-bit values in tenths of their respective units. Multi-byte values are little endian; temperatures and humidity use tenths, and PM uses µg/m³.

Examples: DHT-only packets are 16 bytes, PM-only packets are 18 bytes, MLX-only packets are 16 bytes, and packets with all five sensors are 162 bytes. With no sensors, the device sends a 12-byte status packet with the `no-sensor-data` reason. Presence stays fixed until reset; temporary read failures clear validity but retain the detected sensor's fields (invalid thermal pixels use `INT16_MIN`; invalid PM/DHT/MLX/SVM readings use zero). The receiver checks packet length and masks before decoding. The existing dashboard continues to show unavailable sensors as unavailable.

**Rebuild and upload both boards:** `FFS5` replaces `FFS4` and earlier formats; older receiver firmware cannot decode it. Restart `src/dashboard.py` to load the SVM41 and HW-691 cards. Reset the device after adding sensors or correcting a failed startup connection.

### HW-691 / MLX90614

Connect SDA to GPIO41, SCL to GPIO42, and GND to common ground. Power the breakout according to its marked voltage requirements, keeping SDA/SCL pull-ups at 3.3 V for the ESP32. HW-691 support assumes the MLX90614 I2C module; no sensor address or calibration EEPROM is changed. The default address and API follow the [Adafruit MLX90614 library](https://github.com/adafruit/Adafruit-MLX90614-Library).

A startup message reports `HW-691/MLX90614 found at 0x5A`. Once detected, object and sensor-ambient temperatures are read every second; failed or out-of-range readings are marked invalid and retried. Validity requires both readings within the [manufacturer's calibrated ranges](https://www.melexis.com/en/product/MLX90614/Factory-Calibrated-Infrared-Temperature-Sensor): object −70 to 380 °C and ambient −40 to 125 °C. The device OLED alternates thermal sensors every three seconds when both are present. The receiver prints both readings, and the dashboard has a dedicated HW-691 card.

Object temperature uses the existing fire thresholds (>50 °C Fire, >80 °C Response), with three consecutive assessments required. The hotter available reading from MLX90614 and Grid-EYE is used. A missing HW-691 adds no fields to the radio packet; a detected one adds four bytes. Reset after connecting a sensor that was absent at startup.

### SVM41

Support uses [Sensirion's I2C SVM4x driver](https://github.com/Sensirion/arduino-i2c-svm4x). Connect VDD to 3.3 V, GND and SEL to ground, SDA to GPIO41, and SCL to GPIO42. Ground SEL before powering on. The driver communicates with the complete SVM41 module, not a bare SGP41 sensor or the USB/UART adapter. The [I2C interface specification](https://sensirion.com/media/documents/EBF2BA04/61B8BA7C/Sensirion_Gas_Sensors_SEK-SVM4x_I2C_Interface_Description.pdf) specifies address `0x6A` and a maximum 100 kHz clock, matching the shared bus.

At startup the firmware resets the module, reads its serial-number response, and starts continuous measurement. It waits at least one second before reading temperature, relative humidity, VOC Index, and NOx Index. Read/CRC failures clear validity and retry every second; failure to initialize excludes the sensor until the next board reset. A detected SVM41 adds eight bytes to each LoRa packet, even during a temporary read failure. With only SVM41 attached, packets are 20 bytes.

The device OLED alternates SVM41 readings with the particulate rows. The receiver outputs them over serial and JSON, and the dashboard has a dedicated SVM41 card. VOC and NOx values are unitless indices, not ppm concentrations. SVM41 temperature participates in the heat thresholds, and its humidity is used when DHT11 has no valid reading. VOC/NOx thresholds raise surveillance as described below.

## Build both programs

```sh
pio run -e heltec_wifi_lora_32_V3
pio run -e heltec_lora_server
```

These commands compile without uploading. PlatformIO installs the configured dependencies automatically.

The `build_src_filter` settings select the correct source file for each environment. You do not need to rename files or comment out either program. The file open in your editor does not select the firmware.

## Upload the device

1. Connect only the board with the sensors attached over USB.
2. Find its serial port:

   ```sh
   pio device list
   ```

3. Replace `/dev/cu.usbserial-XXXX` below with that board's actual port:

   ```sh
   pio run -e heltec_wifi_lora_32_V3 -t upload --upload-port /dev/cu.usbserial-XXXX
   ```

4. Open its serial monitor:

   ```sh
   pio device monitor --port /dev/cu.usbserial-XXXX --baud 115200
   ```

The upload command also builds the program. The port shown is a macOS example; Windows ports typically look like `COM5`, and Linux ports like `/dev/ttyUSB0` or `/dev/ttyACM0`.

Press the board's reset button while monitoring to see startup messages. A working device should report `LoRa at 915.0 MHz: ready (0)`, detect the sensors, print readings, and report `LoRa packet ...: sent (0)`. I2C scans repeat every 10 seconds. DHT reads and LoRa send attempts occur approximately every two seconds; PMSA003I reads occur approximately every second.

Exit the monitor with **Ctrl+C** before uploading again.

## Upload the server

1. Disconnect the device's USB cable and connect the board intended as the receiver.
2. Run `pio device list` again to identify its port.
3. Replace `/dev/cu.usbserial-YYYY` with the actual receiver port:

   ```sh
   pio run -e heltec_lora_server -t upload --upload-port /dev/cu.usbserial-YYYY
   ```

4. Open the receiver's serial monitor:

   ```sh
   pio device monitor --port /dev/cu.usbserial-YYYY --baud 115200
   ```

On reset, expect `LoRa receiver at 915.0 MHz: ready (0)`.

Always select an environment with `-e` when uploading. The environment selects the program; `--upload-port` selects the physical board. PlatformIO cannot infer which of two identical boards should be the device or server. See the [PlatformIO command reference](https://docs.platformio.org/en/stable/core/userguide/cmd_run.html).

## Run the complete system

1. Power both programmed boards with their antennas attached. The device can use a separate suitable power source while the server stays connected to the computer.
2. Monitor the server at **115200 baud** using its port.
3. Look for increasing `Packet` numbers, RSSI/SNR, the 8×8 thermal readings, particulate measurements, and temperature/humidity. The receiver OLED shows packet number, RSSI, PM2.5, and DHT readings after receiving a valid packet.

Both source files currently set `LORA_FREQUENCY_MHZ` to `915.0f`. Keep radio settings identical on both boards and use a frequency appropriate for your hardware and deployment region. If you change the frequency, rebuild and upload both programs.

When both boards are connected to the computer, identify their ports by connecting one at a time, then use the explicit port in every upload and monitor command. You can run one monitor per board in separate terminals. Recheck port names after reconnecting USB.

## Fire detection

The device board checks its readings against predefined thresholds once per second. The thresholds and logic are in `include/fire_detection.h`; `checkForFire()` in `src/device.cpp` feeds them the current readings.

### Levels

| Level | Meaning | Device output |
| --- | --- | --- |
| `NORMAL` | No threshold crossed | LED off |
| `SURVEILLANCE` | Dry air and elevated PM2.5 together, or elevated VOC/NOx Index | LED dim |
| `FIRE` | A fire threshold crossed | LED blinking |
| `FIRE >80C` | Automatic-response temperature crossed | LED blinking |

Fire thresholds use OR: any one sensor crossing its fire threshold raises `FIRE`, so a fire seen by only one sensor is not missed. Surveillance thresholds use AND: `SURVEILLANCE` is raised only when every risk sensor with a valid reading (currently humidity and PM2.5) crosses its surveillance threshold. Dry air alone, which is common indoors, or haze in humid air therefore stays `NORMAL`, although the crossed threshold is still listed in the reasons. If one of these sensors has no valid reading, the other one decides alone. The highest level reached wins. A level is confirmed only after it appears in 3 consecutive checks (about 3 seconds), so a single bad reading does not raise an alarm. The confirmed level drops as soon as readings recover. The confirmed level and the crossed thresholds are shown in these places:

- Device: OLED bottom line, onboard LED, and serial output as `Fire check: ...` every second.
- Server: sent in every LoRa packet, shown on the server OLED as `Fire: ...`, and printed to serial as `Fire: <LEVEL> (reasons: ...)`.
- Dashboard: a colored **Fire status** banner at the top of the page.

The server shows the level from the latest packet, so it can lag the device by up to one send interval (about 2 seconds).

A sensor without a current valid reading is skipped. If no sensor has a valid reading, the level stays `NORMAL` and the serial line reports `no-sensor-data`.

### Thresholds and sensor status

| Measurement | Sensor | Status | Normal | Surveillance | Fire |
| --- | --- | --- | --- | --- | --- |
| Temperature | Grid-EYE (hottest pixel), MLX90614 (object), SVM41 (ambient) | Connected, active | ≤ 50 °C | — | > 50 °C; automatic response > 80 °C |
| Relative humidity | DHT11, with SVM41 fallback | Connected, active | ≥ 50 % | < 50 % | — (dry air alone never means fire) |
| PM2.5 | PMSA003I | Connected, active | ≤ 50 µg/m³ | > 50 to 150 µg/m³ | > 150 µg/m³ |
| Flame | Flame sensor | Not connected | — | — | Reading below 100, or 760–1100 nm flame radiation detected |
| CO₂ | Gas sensor | Not connected | — | — | Estimated CO₂ above 30 % |
| Smoke (12-bit ADC, 0–4095) | Smoke sensor | Not connected | ≤ 1190 | > 1190 to 1984 | > 1984 |
| VOC Index | SVM41 | Provisional risk warning | ≤ 150 | > 150 | No gas-only fire alarm |
| NOx Index | SVM41 | Provisional risk warning | ≤ 50 | > 50 | No gas-only fire alarm |

Thresholds come from a prior study ([8] in the project report). The study notes that VOCs can signal a fire before visible flames but are hard to detect outdoors because they dilute quickly, so no VOC threshold was defined.

Notes on the active sensors:

- The DHT11 measures only up to 50 °C, so it cannot report a fire temperature alone. The hottest valid value from Grid-EYE, MLX90614 object temperature, and SVM41 ambient temperature provides the fire temperature. DHT temperature and MLX ambient temperature are transmitted but do not trigger fire detection.
- The Grid-EYE's hottest pixel is a surface temperature, not air temperature. Strongly sunlit surfaces can exceed 50 °C.
- The standard Grid-EYE measures up to about 80 °C, so the > 80 °C level may not trigger unless the sensor is the high-gain model.

### SVM41 threshold rationale

`VOC_SURVEILLANCE_INDEX = 150` and `NOX_SURVEILLANCE_INDEX = 50` in `include/fire_detection.h` are configurable prototype defaults. Either index strictly above its threshold independently raises Surveillance, even when PM/humidity are normal. Both use the existing three-consecutive-assessment confirmation. Gas indices alone never raise Fire or Response, including when both are high. Invalid values, zero, and indices outside 1–500 are ignored.

[Sensirion's VOC note](https://sensirion.com/media/documents/02232963/6294E043/Info_Note_VOC_Index.pdf) gives >150 as an example air-purifier trigger, not a fire threshold. The [NOx note](https://sensirion.com/media/documents/9F289B95/6294DFFC/Info_Note_NOx_Index.pdf) describes a baseline near 1; >50 is our project-selected warning threshold. These relative indices respond to other pollution sources and adapt to recent history. Neither value is a manufacturer-validated wildfire detection limit; validate and tune them against deployment data.

SVM41 ambient temperature joins the existing >50 °C Fire and >80 °C Response checks. DHT11 remains the preferred humidity source, with SVM41 used only when DHT11 has no valid reading; the dry-air threshold remains <50% RH. Gas warnings appear as `VOC-high` / `NOx-high` in serial, receiver, and dashboard. Upload both firmware programs to display the new reason flags correctly; the packet layout remains FFS5.

### Adding a sensor that is not yet connected

1. Wire it to the device board and add a row to the wiring table above.
2. Read it in `src/device.cpp` and keep a validity flag, like the existing sensors.
3. Add its thresholds as constants and a field in `fire::Readings` in `include/fire_detection.h`, and evaluate them in `fire::assess()`. Add a `REASON_...` bit for it and print that bit in `printFireReasons()` in `src/device.cpp`.
4. Pass the reading from `checkForFire()` in `src/device.cpp`, using `NAN` when the reading is invalid.
5. The fire level and reasons already reach the server and dashboard; new `REASON_...` bits need only a label in `fire::describeReasons()`. To send the raw reading too, extend the packet format in both `src/device.cpp` and `src/server.cpp`, update `include/sensor_packet.h`, and change the packet magic (currently `FFS5`) so old firmware ignores the new format.
6. Update the table above from `Not connected` to `Connected, active`.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No USB serial port | Use a data-capable USB cable, check the connection, and rerun `pio device list`. |
| Upload cannot open the port | Close other serial monitors and confirm the selected port belongs to the intended board. |
| Garbled serial output | Set the monitor to 115200 baud. |
| `PMSA003I NOT found at 0x12` | Check power, common ground, SDA on GPIO41, SCL on GPIO42, and that the module uses I2C. The periodic scan should show `0x12`. |
| `PMSA003I read failed` | The startup initialization succeeded, but a measurement read failed. Check supply stability and I2C wiring/pull-ups; reads retry each second. |
| `DHT11 read failed on GPIO4 and GPIO3` | Check DATA on GPIO4 or GPIO3, sensor power/ground, the pull-up, and whether the actual sensor is DHT11 or DHT22. Initialization logs alone do not prove the sensor responds. |
| Grid-EYE appears in a scan but remains “not found” | Its startup probe may have failed. Check connections and power, then reset the device. An address ACK alone does not prove reliable measurement reads. |
| Server prints `Ignored packet: ... wrong protocol` | The two boards run firmware with different packet formats. Rebuild and upload both programs. |
| Server reports sensor `unavailable` | A packet arrived, but the device marked that sensor's reading invalid. Inspect the device's serial output. |
| Device reports packets sent but server receives none | Confirm both boards run their respective programs, use matching radio settings, have antennas attached, and are powered. A successful transmit log is not a receiver acknowledgement. |
| `LoRa ... initialization failed` | Confirm the physical board is the configured Heltec WiFi LoRa 32 V3 and inspect the reported error code. |

**Startup detection:** The sensor set is fixed at startup. The repeated I2C scan is diagnostic only. If a sensor is absent or fails startup detection, fix its connection or power and reset the device. Sensors detected at startup continue to retry measurement reads after temporary failures.
