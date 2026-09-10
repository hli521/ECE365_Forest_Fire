#include <Arduino.h>

#define HELTEC_POWER_BUTTON
#include <heltec_unofficial.h>
#include <SparkFun_GridEYE_Arduino_Library.h>
#include <Adafruit_PM25AQI.h>

// Grid-EYE default I2C address is 0x69 (jumper open).
// Solder the ADDR jumper closed on the breakout to switch it to 0x68.
GridEYE gridEye;
TwoWire gridEyeWire(1);
constexpr int GRID_EYE_SDA = 41;
constexpr int GRID_EYE_SCL = 42;
bool gridEyeConnected = false;
Adafruit_PM25AQI airQuality;
bool airQualityConnected = false;
PM25_AQI_Data airData;
bool airReadingValid = false;
unsigned long lastAirRead = 0;
unsigned long lastAddressScan = 0;
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

    // The OLED uses Wire on GPIO17/18. Use the second I2C controller
    // for both external sensors wired to GPIO41/42.
    gridEyeWire.begin(GRID_EYE_SDA, GRID_EYE_SCL, 400000);

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

    gridEye.begin(gridEyeAddr, gridEyeWire);
    Serial.printf("Grid-EYE %s at 0x%02X\n", gridEyeConnected ? "found" : "NOT found", gridEyeAddr);

    airQualityConnected = airQuality.begin_I2C(&gridEyeWire);
    Serial.printf("PMSA003I %s at 0x12\n", airQualityConnected ? "found" : "NOT found");

    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
}

void loop() {
    if (millis() - lastAddressScan >= 10000) {
        scanSensorAddresses();
    }
    display.clear();
    display.drawString(0, 0, "Heltec V3 + Grid-EYE");

    if (!gridEyeConnected) {
        display.drawString(0, 16, "Grid-EYE not found!");
        display.drawString(0, 28, "Check wiring/address");
        Serial.println("Grid-EYE not detected on I2C bus");
    } else {
        float minTemp = 1000, maxTemp = -1000, sumTemp = 0;
        int goodReads = 0;
        for (int i = 0; i < 64; i++) {
            float t = gridEye.getPixelTemperature(i);
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
        if (PRINT_GRID_PIXELS) Serial.println("---");

        if (goodReads == 0) {
            display.drawString(0, 16, "Read error (-99)");
            display.drawString(0, 28, "Check I2C wiring");
        } else {
            float avgTemp = sumTemp / goodReads;
            display.drawString(0, 16, "Avg: " + String(avgTemp, 1) + " C");
            display.drawString(0, 28, "Min: " + String(minTemp, 1) + "  Max: " + String(maxTemp, 1));
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
            display.drawString(0, 44, "PM2.5: " + String(airData.pm25_env) + " ug/m3");
        } else {
            display.drawString(0, 44, "PMSA003I: no reading");
        }
    } else {
        display.drawString(0, 44, "PMSA003I not found");
    }

    display.display();
    delay(250);
}
