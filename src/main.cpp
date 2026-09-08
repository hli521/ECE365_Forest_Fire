#include <Arduino.h>

#define HELTEC_POWER_BUTTON 
#include <heltec_unofficial.h>

int counter = 0;

void setup() {
    heltec_setup();
    
    // Set text alignment and font
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
}

void loop() {
    display.clear();

    // Pass coordinates (X, Y) directly into drawString()
    display.drawString(0, 0, "Heltec V3 Active");
    display.drawString(0, 16, "Count: " + String(counter));

    // Push the buffer to the physical OLED
    display.display();

    Serial.printf("Hello from Heltec V3! Count: %d\n", counter);

    counter++;
    delay(1000);
}
