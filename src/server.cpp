// build:
// open new platformIO terminal
// pio run -e heltec_lora_server -t upload --upload-port /dev/cu.usbserial-YYYY
//
// This board only receives LoRa packets and prints them over USB Serial -
// same as your original file, plus one extra line per packet: a compact
// JSON summary (prefixed with "JSON:") that the local dashboard.py script
// (running on your computer) reads and turns into a live webpage.
// No WiFi involved - the board never joins any network.

#include <Arduino.h>
#include <heltec_unofficial.h>
#include <stdint.h>

// Match the frequency and RadioLib defaults used by device.cpp.
constexpr float LORA_FREQUENCY_MHZ = 915.0f;
constexpr size_t PACKET_SIZE = 4 + 4 + 1 + 64 * 2 + 3 * 2 + 2 * 2;
uint8_t packet[PACKET_SIZE];
bool radioReady = false;

uint16_t readU16(size_t &offset) {
  uint16_t value = packet[offset] | (static_cast<uint16_t>(packet[offset + 1]) << 8);
  offset += 2;
  return value;
}

uint32_t readU32(size_t &offset) {
  uint32_t value = readU16(offset);
  return value | (static_cast<uint32_t>(readU16(offset)) << 16);
}

void printJsonLine(uint32_t sequence, uint8_t valid, int16_t gridEye[64],
                    uint16_t pm1, uint16_t pm25, uint16_t pm10,
                    int16_t temperature, int16_t humidity) {
  // One self-contained JSON object per line, prefixed so it's easy for the
  // Python side to pick out among the human-readable debug prints.
  Serial.print("JSON:{");
  Serial.printf("\"sequence\":%lu,", static_cast<unsigned long>(sequence));
  Serial.printf("\"rssi\":%.1f,", radio.getRSSI());
  Serial.printf("\"snr\":%.1f,", radio.getSNR());

  bool gridValid = valid & 1;
  Serial.printf("\"gridEyeValid\":%s,", gridValid ? "true" : "false");
  Serial.print("\"gridEye\":[");
  for (int i = 0; i < 64; ++i) {
    if (gridEye[i] == INT16_MIN) Serial.print("null");
    else Serial.print(gridEye[i] / 10.0f, 1);
    if (i < 63) Serial.print(",");
  }
  Serial.print("],");

  bool pmValid = valid & 2;
  Serial.printf("\"pmValid\":%s,", pmValid ? "true" : "false");
  Serial.printf("\"pm1\":%u,\"pm25\":%u,\"pm10\":%u,", pm1, pm25, pm10);

  bool dhtValid = valid & 4;
  Serial.printf("\"dhtValid\":%s,", dhtValid ? "true" : "false");
  Serial.printf("\"temperature\":%.1f,\"humidity\":%.1f", temperature / 10.0f, humidity / 10.0f);
  Serial.println("}");
}

void setup() {
  heltec_setup();
  int16_t state = radio.begin(LORA_FREQUENCY_MHZ);
  radioReady = state == RADIOLIB_ERR_NONE;
  Serial.printf("LoRa receiver at %.1f MHz: %s (%d)\n", LORA_FREQUENCY_MHZ,
                radioReady ? "ready" : "initialization failed", state);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
}

void loop() {
  if (!radioReady) {
    delay(1000);
    return;
  }

  size_t length = PACKET_SIZE;
  int16_t state = radio.receive(packet, length);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("LoRa receive error: %d\n", state);
    return;
  }
  if (length != PACKET_SIZE || memcmp(packet, "FFS1", 4) != 0) {
    Serial.printf("Ignored packet: %u bytes or wrong protocol\n",
                  static_cast<unsigned>(length));
    return;
  }

  size_t offset = 4;
  uint32_t sequence = readU32(offset);
  uint8_t valid = packet[offset++];
  Serial.printf("\nPacket %lu, RSSI %.1f dBm, SNR %.1f dB\n",
                static_cast<unsigned long>(sequence), radio.getRSSI(), radio.getSNR());
  Serial.printf("Grid-EYE: %s\n", valid & 1 ? "valid" : "unavailable");
  int16_t gridEye[64];
  for (int i = 0; i < 64; ++i) {
    int16_t pixel = static_cast<int16_t>(readU16(offset));
    gridEye[i] = pixel;
    if (pixel == INT16_MIN) Serial.print("ERR");
    else Serial.print(pixel / 10.0f, 1);
    Serial.print(i % 8 == 7 ? '\n' : '\t');
  }
  uint16_t pm1 = readU16(offset);
  uint16_t pm25 = readU16(offset);
  uint16_t pm10 = readU16(offset);
  int16_t temperature = static_cast<int16_t>(readU16(offset));
  int16_t humidity = static_cast<int16_t>(readU16(offset));
  if (valid & 2) {
    Serial.printf("PMSA003I: PM1=%u PM2.5=%u PM10=%u ug/m3\n", pm1, pm25, pm10);
  } else Serial.println("PMSA003I: unavailable");
  if (valid & 4) {
    Serial.printf("DHT11: %.1f C, %.1f %% RH\n",
                  temperature / 10.0f, humidity / 10.0f);
  } else Serial.println("DHT11: unavailable");

  printJsonLine(sequence, valid, gridEye, pm1, pm25, pm10, temperature, humidity);

  display.clear();
  display.drawString(0, 0, "Packet " + String(sequence));
  display.drawString(0, 12, "RSSI " + String(radio.getRSSI(), 1) + " dBm");
  display.drawString(0, 24, valid & 2 ? "PM2.5 " + String(pm25) + " ug/m3" : "PM unavailable");
  display.drawString(0, 36, valid & 4 ? "T " + String(temperature / 10.0f, 1) +
                                      "C H " + String(humidity / 10.0f, 1) + "%" : "DHT unavailable");
  display.display();
}