#include <Arduino.h>

#define HELTEC_POWER_BUTTON
#include <heltec_unofficial.h>
#include <SparkFun_GridEYE_Arduino_Library.h>

// Grid-EYE default I2C address is 0x69 (jumper open).
// Solder the ADDR jumper closed on the breakout to switch it to 0x68.
GridEYE gridEye;
bool gridEyeConnected = false;

// Probe an address with a plain I2C transaction (no register access) to see
// if anything acks -- the GridEYE library's begin() never checks this.
bool i2cPing(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void setup() {
    heltec_setup();

    // The OLED init above already brings up the shared I2C bus on
    // SDA_OLED/SCL_OLED (GPIO17/18) at 700kHz. The AMG8833 is only rated
    // for Fast-mode I2C (400kHz max), so back the clock off before talking
    // to it -- the OLED is fine at 400kHz too.
    Wire.setClock(400000);

    Serial.println("Scanning I2C bus...");
    for (uint8_t addr = 1; addr < 127; addr++) {
        if (i2cPing(addr)) {
            Serial.printf("  Found device at 0x%02X\n", addr);
        }
    }

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

    gridEye.begin(gridEyeAddr);
    Serial.printf("Grid-EYE %s at 0x%02X\n", gridEyeConnected ? "found" : "NOT found", gridEyeAddr);

    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
}

void loop() {
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
            Serial.print(t);
            Serial.print(i % 8 == 7 ? '\n' : '\t');

            if (t == -99.0f) continue; // per-pixel read error, skip it
            minTemp = min(minTemp, t);
            maxTemp = max(maxTemp, t);
            sumTemp += t;
            goodReads++;
        }
        Serial.println("---");

        if (goodReads == 0) {
            display.drawString(0, 16, "Read error (-99)");
            display.drawString(0, 28, "Check I2C wiring");
        } else {
            float avgTemp = sumTemp / goodReads;
            display.drawString(0, 16, "Avg: " + String(avgTemp, 1) + " C");
            display.drawString(0, 28, "Min: " + String(minTemp, 1) + "  Max: " + String(maxTemp, 1));
        }
    }

    display.display();
    delay(250);
}
