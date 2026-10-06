// build:
// open new platformIO terminal
//pio run -e heltec_wifi_lora_32_V3 -t upload --upload-port /dev/cu.usbserial-XXXX

#include <Arduino.h>

#define HELTEC_POWER_BUTTON
#include <heltec_unofficial.h>
#include <SparkFun_GridEYE_Arduino_Library.h>
#include <Adafruit_PM25AQI.h>
#include <Adafruit_MLX90614.h>
#include <DHT.h>              // Adafruit DHT sensor library
#include <Adafruit_Sensor.h>  // Adafruit Unified Sensor (DHT library dependency)
#include <stdint.h>
#include "fire_detection.h"
#include "sensor_packet.h"

// Grid-EYE default I2C address is 0x69 (jumper open).
// Solder the ADDR jumper closed on the breakout to switch it to 0x68.
GridEYE gridEye;
TwoWire gridEyeWire(1);
constexpr int GRID_EYE_SDA = 41;
constexpr int GRID_EYE_SCL = 42;
constexpr uint32_t SENSOR_I2C_HZ = 100000;
bool gridEyeConnected = false;
Adafruit_PM25AQI airQuality;
bool airQualityConnected = false;
PM25_AQI_Data airData;
bool airReadingValid = false;
unsigned long lastAirRead = 0;
unsigned long lastAddressScan = 0;
unsigned long lastGridStatus = 0;

// --- HW-691 (MLX90614) on the shared external I2C bus ---
constexpr uint8_t MLX_ADDRESS = 0x5A;
constexpr unsigned long MLX_READ_INTERVAL_MS = 1000;
Adafruit_MLX90614 mlx;
bool mlxConnected = false;
bool mlxReadingValid = false;
float mlxObjectC = NAN;
float mlxAmbientC = NAN;
unsigned long lastMlxRead = 0;

void readMlx() {
  mlxObjectC = mlx.readObjectTempC();
  mlxAmbientC = mlx.readAmbientTempC();
  // Reject failed transfers and readings outside the calibrated ranges.
  // This also rejects the sensor's raw error-bit values.
  mlxReadingValid = isfinite(mlxObjectC) && isfinite(mlxAmbientC) &&
                    mlxObjectC >= -70.0f && mlxObjectC <= 380.0f &&
                    mlxAmbientC >= -40.0f && mlxAmbientC <= 125.0f;
  lastMlxRead = millis();
}

// --- DHT11 setup ---
constexpr int DHT_PIN = 4;
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);
constexpr unsigned long DHT_READ_INTERVAL_MS = 2000; // DHT11 max ~1 Hz; 2s is safely conservative
unsigned long lastDhtRead = 0;
float dhtTempC = NAN;
float dhtHumidity = NAN;
bool dhtReadingValid = false;
bool dhtConnected = false;
uint8_t sensorsPresent = 0;

// --- Fire detection (thresholds in include/fire_detection.h) ---
constexpr unsigned long FIRE_CHECK_INTERVAL_MS = 1000;
unsigned long lastFireCheck = 0;
float gridMaxTempC = NAN;
fire::Detector fireDetector;
fire::Level fireLevel = fire::Level::Normal;
uint8_t fireReasons = 0;

// Both Heltec LoRa V3 boards must use these same radio settings. Choose a
// frequency allowed in the deployment region (915 MHz is for the US).
constexpr float LORA_FREQUENCY_MHZ = 915.0f;
constexpr unsigned long LORA_SEND_INTERVAL_MS = 2000;
constexpr int16_t INVALID_TEMPERATURE = INT16_MIN;
constexpr size_t PACKET_SIZE = sensor_packet::MAX_SIZE;
uint8_t packet[PACKET_SIZE];
int16_t gridPixels[64];
bool gridReadingValid = false;
bool radioReady = false;
uint32_t packetSequence = 0;
unsigned long lastLoraSend = 0;
unsigned long txStarted = 0;
unsigned long txTimeoutMs = 0;
bool txInProgress = false;
bool lastTxSucceeded = false;

// FFS4 layout and sensor bits are defined in sensor_packet.h.
// Invalid detected sensors retain their fields: Grid-EYE uses INT16_MIN,
// PM, DHT, and MLX use zero. Receivers must check validity before using values.
void putU16(size_t &offset, uint16_t value) {
  packet[offset++] = static_cast<uint8_t>(value);
  packet[offset++] = static_cast<uint8_t>(value >> 8);
}

void putU32(size_t &offset, uint32_t value) {
  putU16(offset, static_cast<uint16_t>(value));
  putU16(offset, static_cast<uint16_t>(value >> 16));
}

int16_t toTenths(float value) {
  return static_cast<int16_t>(lroundf(value * 10.0f));
}

void sendSensorPacket() {
  if (!radioReady || txInProgress) return;

  size_t offset = 0;
  packet[offset++] = 'F';
  packet[offset++] = 'F';
  packet[offset++] = 'S';
  packet[offset++] = '4';
  putU32(offset, packetSequence++);
  packet[offset++] = sensorsPresent;
  packet[offset++] = (gridReadingValid ? 1 : 0) |
                     (airReadingValid ? 2 : 0) |
                     (dhtReadingValid ? 4 : 0) |
                     (mlxReadingValid ? sensor_packet::MLX : 0);
  if (sensorsPresent & sensor_packet::GRID) {
    for (int16_t pixel : gridPixels) putU16(offset, static_cast<uint16_t>(pixel));
  }
  if (sensorsPresent & sensor_packet::AIR) {
    putU16(offset, airReadingValid ? airData.pm10_env : 0);
    putU16(offset, airReadingValid ? airData.pm25_env : 0);
    putU16(offset, airReadingValid ? airData.pm100_env : 0);
  }
  if (sensorsPresent & sensor_packet::DHT) {
    putU16(offset, dhtReadingValid ? static_cast<uint16_t>(toTenths(dhtTempC)) : 0);
    putU16(offset, dhtReadingValid ? static_cast<uint16_t>(toTenths(dhtHumidity)) : 0);
  }
  if (sensorsPresent & sensor_packet::MLX) {
    putU16(offset, mlxReadingValid ? static_cast<uint16_t>(toTenths(mlxObjectC)) : 0);
    putU16(offset, mlxReadingValid ? static_cast<uint16_t>(toTenths(mlxAmbientC)) : 0);
  }
  packet[offset++] = static_cast<uint8_t>(fireLevel);
  packet[offset++] = fireReasons;

  // startTransmit returns while the radio sends the packet. Keep packet[]
  // unchanged until finishTransmit so the OLED and sensors can keep updating.
  int16_t result = radio.startTransmit(packet, offset);
  txInProgress = result == RADIOLIB_ERR_NONE;
  if (txInProgress) {
    txStarted = millis();
    txTimeoutMs = 5 + (radio.getTimeOnAir(offset) * 5) / 1000;
  } else {
    lastTxSucceeded = false;
  }
  Serial.printf("LoRa packet %lu (%u bytes): %s (%d)\n",
                static_cast<unsigned long>(packetSequence - 1),
                static_cast<unsigned>(offset),
                txInProgress ? "transmitting" : "start failed", result);
}

void serviceLoraTransmit() {
  if (!txInProgress) return;
  bool done = digitalRead(DIO1) == HIGH;
  bool timedOut = millis() - txStarted > txTimeoutMs;
  if (!done && !timedOut) return;

  int16_t result = radio.finishTransmit();
  txInProgress = false;
  lastTxSucceeded = done && result == RADIOLIB_ERR_NONE;
  Serial.printf("LoRa packet %lu: %s (%d)\n",
                static_cast<unsigned long>(packetSequence - 1),
                lastTxSucceeded ? "sent" : "failed", result);
}

// Use the hotter of Grid-EYE and MLX object temperature for detection.
// DHT contributes humidity; DHT and MLX ambient temperatures are telemetry only.
void checkForFire() {
  fire::Readings readings;
  if (gridReadingValid) readings.maxTempC = gridMaxTempC;
  if (mlxReadingValid && (isnan(readings.maxTempC) || mlxObjectC > readings.maxTempC)) {
    readings.maxTempC = mlxObjectC;
  }
  if (dhtReadingValid) {
    readings.humidityPct = dhtHumidity;
  }
  if (airReadingValid) readings.pm25 = airData.pm25_env;

  fire::Assessment assessment = fire::assess(readings);
  fire::Level previous = fireLevel;
  fireLevel = fireDetector.update(assessment.level);
  fireReasons = assessment.reasons;

  char reasonText[64];
  fire::describeReasons(assessment.reasons, reasonText, sizeof(reasonText));
  Serial.printf("Fire check: %s (current reading %s; max temp %.1f C, RH %.1f %%, PM2.5 %.0f ug/m3; reasons: %s)\n",
                fire::levelName(fireLevel), fire::levelName(assessment.level),
                readings.maxTempC, readings.humidityPct, readings.pm25, reasonText);
  if (fireLevel != previous) {
    Serial.printf("*** FIRE LEVEL CHANGED: %s -> %s ***\n",
                  fire::levelName(previous), fire::levelName(fireLevel));
  }
}

// Onboard LED: off when normal, dim when under surveillance,
// blinking when a fire is confirmed.
void updateFireLed() {
  switch (fireLevel) {
    case fire::Level::Normal: heltec_led(0); break;
    case fire::Level::Surveillance: heltec_led(5); break;
    default: heltec_led((millis() / 250) % 2 ? 100 : 0); break;
  }
}

// Enable only when the full thermal image is needed; keep scans easy to read.
constexpr bool PRINT_GRID_PIXELS = false;

// Probe an address with a plain I2C transaction (no register access) to see
// if anything acks -- the GridEYE library's begin() never checks this.
bool i2cPing(uint8_t addr) {
  gridEyeWire.beginTransmission(addr);
  return gridEyeWire.endTransmission() == 0;
}

void scanSensorAddresses() {
  Serial.println("\n=== I2C address scan: SDA=41, SCL=42 ===");
  unsigned int found = 0;
  unsigned int errors = 0;
  // Scan usable 7-bit addresses, excluding reserved ranges.
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    gridEyeWire.beginTransmission(addr);
    uint8_t result = gridEyeWire.endTransmission();
    if (result == 0) {
      const char *label = "unknown device";
      if (addr == MLX_ADDRESS) label = "expected HW-691/MLX90614 address";
      if (addr == 0x12) label = "expected PMSA003I address";
      if (addr == 0x68 || addr == 0x69) label = "expected Grid-EYE address";
      Serial.printf("  ACK at 0x%02X (%s)\n", addr, label);
      found++;
    } else if (result != 2) { // Address NACK normally means no device.
      Serial.printf("  Bus error at 0x%02X: code %u\n", addr,
                    static_cast<unsigned>(result));
      errors++;
    }
  }
  Serial.printf("Scan complete: %u responding address(es), %u bus error(s)\n", found, errors);
  Serial.println("Address labels are hints, not verified sensor identities.");
  Serial.println("=== Scan repeats every 10 seconds ===\n");
  lastAddressScan = millis();
}

void setup() {
  heltec_setup();
  for (int16_t &pixel : gridPixels) pixel = INVALID_TEMPERATURE;
  int16_t radioState = radio.begin(LORA_FREQUENCY_MHZ);
  radioReady = radioState == RADIOLIB_ERR_NONE;
  Serial.printf("LoRa at %.1f MHz: %s (%d)\n", LORA_FREQUENCY_MHZ,
                radioReady ? "ready" : "initialization failed", radioState);

  // The OLED uses Wire on GPIO17/18. Use the second I2C controller
  // for the external I2C sensors wired to GPIO41/42.
  // Use a slower clock while diagnosing unreliable measurement reads.
  if (!gridEyeWire.begin(GRID_EYE_SDA, GRID_EYE_SCL, SENSOR_I2C_HZ)) {
    Serial.println("ERROR: sensor I2C bus initialization failed");
  }

  dht.begin();

  // Allow the PMSA003I to boot before probing the shared sensor bus.
  delay(3000);
  scanSensorAddresses();

  // Try the default address (jumper open) first, then the jumper-closed
  // address, and use whichever one actually acks.
  uint8_t gridEyeAddr = 0x69;
  if (i2cPing(0x69)) {
    gridEyeAddr = 0x69;
    gridEyeConnected = true;
  } else if (i2cPing(0x68)) {
    gridEyeAddr = 0x68;
    gridEyeConnected = true;
  } else {
    gridEyeConnected = false;
  }

  if (gridEyeConnected) gridEye.begin(gridEyeAddr, gridEyeWire);
  Serial.printf("Grid-EYE %s at 0x%02X\n", gridEyeConnected ? "found" : "NOT found", gridEyeAddr);

  airQualityConnected = airQuality.begin_I2C(&gridEyeWire);
  gridEyeWire.setClock(SENSOR_I2C_HZ);
  Serial.printf("Sensor I2C clock: %lu Hz\n",
                static_cast<unsigned long>(gridEyeWire.getClock()));
  Serial.printf("PMSA003I %s at 0x12\n", airQualityConnected ? "found" : "NOT found");

  mlxConnected = mlx.begin(MLX_ADDRESS, &gridEyeWire);
  if (mlxConnected) readMlx();
  Serial.printf("HW-691/MLX90614 %s at 0x%02X\n",
                mlxConnected ? "found" : "NOT found", MLX_ADDRESS);

  // DHT has no address probe. Require a successful measurement, allowing
  // three attempts so a single startup read failure does not exclude it.
  for (int attempt = 0; attempt < 3; ++attempt) {
    dhtHumidity = dht.readHumidity();
    dhtTempC = dht.readTemperature();
    dhtConnected = !isnan(dhtHumidity) && !isnan(dhtTempC);
    if (dhtConnected) break;
    if (attempt < 2) delay(DHT_READ_INTERVAL_MS);
  }
  lastDhtRead = millis();
  dhtReadingValid = dhtConnected;
  sensorsPresent = (gridEyeConnected ? sensor_packet::GRID : 0) |
                   (airQualityConnected ? sensor_packet::AIR : 0) |
                   (dhtConnected ? sensor_packet::DHT : 0) |
                   (mlxConnected ? sensor_packet::MLX : 0);
  Serial.printf("DHT11 %s on GPIO%d\n", dhtConnected ? "found" : "NOT found", DHT_PIN);
  Serial.printf("Startup sensors: 0x%02X; FFS4 packets: %u bytes\n",
                sensorsPresent, static_cast<unsigned>(sensor_packet::packetSize(sensorsPresent)));

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
}

void loop() {
  serviceLoraTransmit();
  if (millis() - lastAddressScan >= 10000) {
    scanSensorAddresses();
  }
  display.clear();

  if (!gridEyeConnected) {
    display.drawString(0, 0, "Grid-EYE not found!");
    display.drawString(0, 12, "Check wiring/address");
    Serial.println("Grid-EYE not detected on I2C bus");
  } else {
    float minTemp = 1000, maxTemp = -1000, sumTemp = 0;
    int goodReads = 0;
    for (int i = 0; i < 64; i++) {
      float t = gridEye.getPixelTemperature(i);
      gridPixels[i] = t == -99.0f ? INVALID_TEMPERATURE : toTenths(t);
      if (PRINT_GRID_PIXELS) {
        Serial.print(t);
        Serial.print(i % 8 == 7 ? '\n' : '\t');
      }

      if (t == -99.0f) continue; // per-pixel read error, skip it
      minTemp = min(minTemp, t);
      maxTemp = max(maxTemp, t);
      sumTemp += t;
      goodReads++;
    }
    gridReadingValid = goodReads > 0;
    gridMaxTempC = gridReadingValid ? maxTemp : NAN;
    if (PRINT_GRID_PIXELS) Serial.println("---");
    if (millis() - lastGridStatus >= 1000) {
      lastGridStatus = millis();
      Serial.printf("Grid-EYE: %d/64 pixel reads succeeded, %d failed\n",
                    goodReads, 64 - goodReads);
    }

    if (goodReads == 0) {
      display.drawString(0, 0, "Read error (-99)");
      display.drawString(0, 12, "Check I2C wiring");
    } else {
      float avgTemp = sumTemp / goodReads;
      display.drawString(0, 0, "Avg: " + String(avgTemp, 1) + " C");
      display.drawString(0, 12, "Min: " + String(minTemp, 1) + "  Max: " + String(maxTemp, 1));
    }
  }

  if (airQualityConnected) {
    if (millis() - lastAirRead >= 1000) {
      lastAirRead = millis();
      airReadingValid = airQuality.read(&airData);
      if (airReadingValid) {
        Serial.printf("PMSA003I (environmental): PM1.0=%u, PM2.5=%u, PM10=%u ug/m3\n",
                      static_cast<unsigned>(airData.pm10_env),
                      static_cast<unsigned>(airData.pm25_env),
                      static_cast<unsigned>(airData.pm100_env));
      } else {
        Serial.println("PMSA003I read failed; retrying next second");
      }
    }
    if (airReadingValid) {
      display.drawString(0, 24, "PM2.5: " + String(airData.pm25_env) + " ug/m3");
      display.drawString(0, 34, "PM10: " + String(airData.pm100_env) + " ug/m3");
    } else {
      display.drawString(0, 24, "PMSA003I: no reading");
    }
  } else {
    display.drawString(0, 24, "PMSA003I not found");
    if (millis() - lastAirRead >= 1000) {
      lastAirRead = millis();
      Serial.println("PMSA003I: startup initialization failed; reset board to retry");
    }
  }

  // --- DHT11 read + display ---
  if (dhtConnected && millis() - lastDhtRead >= DHT_READ_INTERVAL_MS) {
    lastDhtRead = millis();
    float h = dht.readHumidity();
    float t = dht.readTemperature(); // Celsius by default

    if (isnan(h) || isnan(t)) {
      dhtReadingValid = false;
      Serial.println("DHT11 read failed (NaN); will retry next interval");
    } else {
      dhtTempC = t;
      dhtHumidity = h;
      dhtReadingValid = true;
      Serial.printf("DHT11: Temp=%.1f C, Humidity=%.1f %%\n", dhtTempC, dhtHumidity);
    }
  }

  if (dhtReadingValid) {
    display.drawString(0, 46, "T:" + String(dhtTempC, 1) + "C H:" + String(dhtHumidity, 1) + "%");
  } else {
    display.drawString(0, 46, dhtConnected ? "DHT11: no reading" : "DHT11 not found");
  }

  if (mlxConnected && millis() - lastMlxRead >= MLX_READ_INTERVAL_MS) {
    readMlx();
    if (mlxReadingValid) {
      Serial.printf("MLX90614: Object=%.1f C, Ambient=%.1f C\n", mlxObjectC, mlxAmbientC);
    } else {
      Serial.println("MLX90614 read failed; retrying next second");
    }
  }
  // Alternate the top OLED rows when both thermal sensors are present.
  if (mlxConnected && (!gridEyeConnected || (millis() / 3000) % 2)) {
    display.setColor(BLACK);
    display.fillRect(0, 0, 128, 24);
    display.setColor(WHITE);
    display.drawString(0, 0, mlxReadingValid ? "IR object: " + String(mlxObjectC, 1) + " C"
                                           : "HW-691: no reading");
    if (mlxReadingValid) display.drawString(0, 12, "IR ambient: " + String(mlxAmbientC, 1) + " C");
  }

  if (millis() - lastFireCheck >= FIRE_CHECK_INTERVAL_MS) {
    lastFireCheck = millis();
    checkForFire();
  }
  updateFireLed();

  display.drawString(0, 56, String(fire::levelName(fireLevel)) + "  " +
                     (!radioReady ? "LoRa err" :
                      txInProgress ? "Tx..." :
                      lastTxSucceeded ? "Tx ok" : "Tx wait"));

  display.display();
  if (millis() - lastLoraSend >= LORA_SEND_INTERVAL_MS) {
    lastLoraSend = millis();
    sendSensorPacket();
  }
  serviceLoraTransmit();
  delay(100);
}
