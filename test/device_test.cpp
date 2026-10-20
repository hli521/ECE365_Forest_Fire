// Standalone DHT11 + HW-691 (MLX90614) diagnostic. Serial: 115200 baud.
// Build: pio run -e heltec_sensor_test
// Upload: pio run -e heltec_sensor_test -t upload --upload-port  /dev/cu.usbserial-YYYY
#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <heltec_unofficial.h>

constexpr int DHT_PINS[] = {3, 4};
int activeDht = 0;
constexpr unsigned long READ_INTERVAL_MS = 2000;

constexpr int SENSOR_SDA = 41;
constexpr int SENSOR_SCL = 42;
constexpr uint8_t MLX_ADDRESS = 0x5A;
TwoWire sensorWire(1);
Adafruit_MLX90614 mlx;
bool busReady = false;
bool mlxReady = false;
unsigned long mlxPass = 0;
DHT dhtSensors[] = {DHT(3, DHT11), DHT(4, DHT11)};
unsigned long lastRead = 0;
unsigned long sample = 0;
unsigned long dhtPass = 0;

void setup() {
  heltec_setup();  // Initialize serial and the onboard OLED.
  // Heltec V3 external sensor power is active-low. Also supports modules
  // powered from Vext; the separate 3V3 rail does not require this switch.
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.drawString(0, 0, "DHT11 + HW-691 test");
  display.drawString(0, 16, "Starting sensor...");
  display.drawString(0, 32, "Readings every 2 sec");
  display.display();
  for (DHT &sensor : dhtSensors) sensor.begin();
  busReady = sensorWire.begin(SENSOR_SDA, SENSOR_SCL, 100000);
  sensorWire.setTimeOut(50);
  delay(3000);  // Sensor startup and time to attach the serial monitor.
  Serial.println("\n=== DHT11 + HW-691 sensor test ===");
  Serial.println("DHT11 DATA=GPIO3 or GPIO4 (automatic detection); Vext enabled.");
  Serial.println("HW-691 SDA=41; SCL=42; address=0x5A.");
  if (!busReady) Serial.println("I2C bus initialization failed; will retry.");
  Serial.println("Readings every 2 seconds.");
  Serial.println("PASS means a valid measurement; failed readings retry automatically.");
}

void loop() {
  if (millis() - lastRead >= READ_INTERVAL_MS) {
    lastRead = millis();
    ++sample;
    Serial.printf("\n--- Sample %lu ---\n", sample);
    float humidity = NAN, temperature = NAN;
    bool dhtValid = false;
    // Prefer the last working pin; try the other pin if its reading fails.
    for (int attempt = 0; attempt < 2; ++attempt) {
      int candidate = (activeDht + attempt) % 2;
      humidity = dhtSensors[candidate].readHumidity();
      temperature = dhtSensors[candidate].readTemperature();
      if (isfinite(humidity) && isfinite(temperature)) {
        activeDht = candidate;
        dhtValid = true;
        break;
      }
    }
    display.clear();
    display.drawString(0, 0, dhtValid ? "DHT11 GPIO" + String(DHT_PINS[activeDht]) : "DHT11 GPIO3 / GPIO4");
    if (dhtValid) {
      ++dhtPass;
      Serial.printf("DHT11 PASS on GPIO%d: temperature=%.1f C, humidity=%.1f %%\n", DHT_PINS[activeDht], temperature, humidity);
      display.drawString(0, 12, String(temperature, 1) + " C  " + String(humidity, 1) + " % RH");
    } else {
      Serial.println("DHT11 FAIL on GPIO3 and GPIO4; check power and DATA pull-up.");
      display.drawString(0, 12, "FAIL - no reading");

    }
    if (!busReady) busReady = sensorWire.begin(SENSOR_SDA, SENSOR_SCL, 100000);
    if (busReady && !mlxReady) mlxReady = mlx.begin(MLX_ADDRESS, &sensorWire);
    float objectC = NAN, ambientC = NAN;
    bool mlxValid = false;
    if (mlxReady) {
      objectC = mlx.readObjectTempC();
      ambientC = mlx.readAmbientTempC();
      mlxValid = isfinite(objectC) && isfinite(ambientC) &&
                 objectC >= -70 && objectC <= 380 && ambientC >= -40 && ambientC <= 125;
      if (!mlxValid) {
        Serial.printf("HW-691 FAIL: invalid reading (object=%.1f, ambient=%.1f); retrying.\n", objectC, ambientC);
        mlxReady = false;
      }
    } else {
      Serial.println("HW-691 FAIL: no response at 0x5A or I2C unavailable; retrying.");
    }
    display.drawString(0, 26, "HW-691 (MLX90614)");
    if (mlxValid) {
      ++mlxPass;
      Serial.printf("HW-691 PASS: object=%.1f C, ambient=%.1f C\n", objectC, ambientC);
      display.drawString(0, 38, "Obj " + String(objectC, 1) + "C Amb " + String(ambientC, 1) + "C");
    } else {
      display.drawString(0, 38, "FAIL - retrying");
    }
    display.drawString(0, 52, "Sample " + String(sample));
    display.display();

    Serial.printf("Valid samples: DHT11 %lu/%lu; HW-691 %lu/%lu\n", dhtPass, sample, mlxPass, sample);
  }
  delay(10);
}
