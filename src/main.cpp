#include <Arduino.h>

#define HELTEC_POWER_BUTTON
#include <heltec_unofficial.h>
#include <SparkFun_GridEYE_Arduino_Library.h>

// Grid-EYE default I2C address is 0x69 (jumper open).
// Solder the ADDR jumper closed on the breakout to switch it to 0x68.
GridEYE gridEye;
bool gridEyeConnected = false;

void setup() {
    heltec_setup();

    // The OLED init above already brings up the shared I2C bus on
    // SDA_OLED/SCL_OLED (GPIO17/18) at 700kHz. The AMG8833 is only rated
    // for Fast-mode I2C (400kHz max), so back the clock off before talking
    // to it -- the OLED is fine at 400kHz too.
    Wire.setClock(400000);

    gridEye.begin();
    // begin() doesn't report failure, so do a real read to confirm the
    // sensor is actually present on the bus.
    gridEyeConnected = !isnan(gridEye.getDeviceTemperature());

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
        for (int i = 0; i < 64; i++) {
            float t = gridEye.getPixelTemperature(i);
            minTemp = min(minTemp, t);
            maxTemp = max(maxTemp, t);
            sumTemp += t;

            Serial.print(t);
            Serial.print(i % 8 == 7 ? '\n' : '\t');
        }
        float avgTemp = sumTemp / 64.0;

        display.drawString(0, 16, "Avg: " + String(avgTemp, 1) + " C");
        display.drawString(0, 28, "Min: " + String(minTemp, 1) + "  Max: " + String(maxTemp, 1));
        Serial.println("---");
    }

    display.display();
    delay(250);
}
