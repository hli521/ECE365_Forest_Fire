# Early-Warning Forest Fire Sensors

Two Heltec WiFi LoRa 32 V3 boards exchange sensor readings over LoRa:

| Role | Source | PlatformIO environment |
| --- | --- | --- |
| Device: reads sensors and transmits readings | `src/device.cpp` | `heltec_wifi_lora_32_V3` |
| Server: receives readings and shows them on OLED and USB serial | `src/server.cpp` | `heltec_lora_server` |

The server is firmware on the second Heltec board. No desktop server, Wi-Fi connection, or cloud service is required. A computer is used to build, upload, and optionally view serial output.

## Requirements

- Two **Heltec WiFi LoRa 32 V3** boards, suitable LoRa antennas, and USB data cables. Attach antennas before powering the radios.
- Grid-EYE thermal sensor, PMSA003I I2C particulate sensor, and DHT11 for the device board.
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
| DHT11 DATA | GPIO4 |
| All sensor grounds | Heltec GND, including any external power supply ground |

Use the power supply specified for your exact sensor or breakout board; the table above covers signal wiring. The [bare PMSA003I module](https://www.adafruit.com/product/4505) needs 5V power and 3.3V I2C logic with external pull-ups. Breakout boards can include power conversion and pull-ups; check their documentation before wiring. Keep signals connected to the Heltec at 3.3V logic levels.

For a bare DHT sensor, use a 4.7–10 kΩ pull-up from DATA to 3.3V; a sensor module may already include one. Check the actual module's pin labels rather than assuming its pin order. See the [DHT wiring guide](https://learn.adafruit.com/dht?view=all).

The device uses the second I2C controller at 100 kHz for GPIO41/42. The onboard OLED uses GPIO17/18 on the other controller. Expected sensor addresses are:

- Grid-EYE: `0x69` by default, or `0x68` with its address jumper closed; the code tries both.
- PMSA003I: `0x12`. Confirm that the sensor is the I2C model.
- DHT11: uses GPIO4 directly and does not appear in an I2C scan.

The code selects `DHT11` in `src/device.cpp`. If your sensor is a DHT22, change `DHT_TYPE` to `DHT22` before building. The current display/log labels still say DHT11.

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
| `SURVEILLANCE` | Fire-risk conditions (dry air or elevated PM2.5) | LED dim |
| `FIRE` | A fire threshold crossed | LED blinking |
| `FIRE >80C` | Automatic-response temperature crossed | LED blinking |

The highest level reached by any sensor wins. A level is confirmed only after it appears in 3 consecutive checks (about 3 seconds), so a single bad reading does not raise an alarm. The confirmed level drops as soon as readings recover. The level is shown on the device OLED's bottom line and printed to the device's serial output as `Fire check: ...`. It is not yet included in the LoRa packet, so the server and dashboard do not show it.

A sensor without a current valid reading is skipped. If no sensor has a valid reading, the level stays `NORMAL` and the serial line reports `no-sensor-data`.

### Thresholds and sensor status

| Measurement | Sensor | Status | Normal | Surveillance | Fire |
| --- | --- | --- | --- | --- | --- |
| Temperature | Grid-EYE (hottest pixel) and DHT11 (air) | Connected, active | ≤ 50 °C | — | > 50 °C; automatic response > 80 °C |
| Relative humidity | DHT11 | Connected, active | ≥ 50 % | < 50 % | — (dry air alone never means fire) |
| PM2.5 | PMSA003I | Connected, active | ≤ 50 µg/m³ | > 50 to 150 µg/m³ | > 150 µg/m³ |
| Flame | Flame sensor | Not connected | — | — | Reading below 100, or 760–1100 nm flame radiation detected |
| CO₂ | Gas sensor | Not connected | — | — | Estimated CO₂ above 30 % |
| Smoke (12-bit ADC, 0–4095) | Smoke sensor | Not connected | ≤ 1190 | > 1190 to 1984 | > 1984 |
| VOCs | — | Not connected, no threshold | — | — | — |

Thresholds come from a prior study ([8] in the project report). The study notes that VOCs can signal a fire before visible flames but are hard to detect outdoors because they dilute quickly, so no VOC threshold was defined.

Notes on the active sensors:

- The DHT11 measures only up to 50 °C, so it cannot report a fire temperature alone. The Grid-EYE's hottest pixel provides the fire temperature.
- The Grid-EYE's hottest pixel is a surface temperature, not air temperature. Strongly sunlit surfaces can exceed 50 °C.
- The standard Grid-EYE measures up to about 80 °C, so the > 80 °C level may not trigger unless the sensor is the high-gain model.

### Adding a sensor that is not yet connected

1. Wire it to the device board and add a row to the wiring table above.
2. Read it in `src/device.cpp` and keep a validity flag, like the existing sensors.
3. Add its thresholds as constants and a field in `fire::Readings` in `include/fire_detection.h`, and evaluate them in `fire::assess()`. Add a `REASON_...` bit for it and print that bit in `printFireReasons()` in `src/device.cpp`.
4. Pass the reading from `checkForFire()` in `src/device.cpp`, using `NAN` when the reading is invalid.
5. To send the reading to the server, extend the packet format in both `src/device.cpp` and `src/server.cpp` and update `PACKET_SIZE` in both files.
6. Update the table above from `Not connected` to `Connected, active`.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No USB serial port | Use a data-capable USB cable, check the connection, and rerun `pio device list`. |
| Upload cannot open the port | Close other serial monitors and confirm the selected port belongs to the intended board. |
| Garbled serial output | Set the monitor to 115200 baud. |
| `PMSA003I NOT found at 0x12` | Check power, common ground, SDA on GPIO41, SCL on GPIO42, and that the module uses I2C. The periodic scan should show `0x12`. |
| `PMSA003I read failed` | The startup initialization succeeded, but a measurement read failed. Check supply stability and I2C wiring/pull-ups; reads retry each second. |
| `DHT11 read failed (NaN)` | Check DATA on GPIO4, sensor power/ground, the pull-up, and whether the actual sensor is DHT11 or DHT22. Initialization logs alone do not prove the sensor responds. |
| Grid-EYE appears in a scan but remains “not found” | Its startup probe may have failed. Check connections and power, then reset the device. An address ACK alone does not prove reliable measurement reads. |
| Server reports sensor `unavailable` | A packet arrived, but the device marked that sensor's reading invalid. Inspect the device's serial output. |
| Device reports packets sent but server receives none | Confirm both boards run their respective programs, use matching radio settings, have antennas attached, and are powered. A successful transmit log is not a receiver acknowledgement. |
| `LoRa ... initialization failed` | Confirm the physical board is the configured Heltec WiFi LoRa 32 V3 and inspect the reported error code. |

**Current startup limitation:** Grid-EYE detection and PMSA003I initialization happen only once. If either fails at startup, the repeated I2C scan does not reinitialize it. Fix the connection or power issue and reset the device. DHT reads retry automatically.
