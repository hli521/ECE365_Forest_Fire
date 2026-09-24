// Threshold-based fire detection for the device board.
//
// Only the thresholds for sensors on this board are applied: temperature
// (Grid-EYE and DHT11), relative humidity (DHT11), and PM2.5 (PMSA003I).
// The reference study's flame, CO2, smoke-ADC, and VOC thresholds need
// sensors that are not connected yet. The README's "Fire detection" section
// lists every threshold, its sensor, and how to add a sensor here.
//
// This header has no Arduino dependencies so the logic can be tested on a
// computer.

#pragma once

#include <math.h>
#include <stdint.h>

namespace fire {

constexpr float FIRE_TEMP_C = 50.0f;            // fire condition above this
constexpr float RESPONSE_TEMP_C = 80.0f;        // automatic response above this
constexpr float DRY_HUMIDITY_PCT = 50.0f;       // surveillance below this
constexpr float PM25_SURVEILLANCE_UGM3 = 50.0f; // surveillance above this
constexpr float PM25_FIRE_UGM3 = 150.0f;        // fire condition above this

enum class Level : uint8_t { Normal, Surveillance, Fire, Response };

// Reasons are a bitmask so every threshold that was crossed can be reported.
enum Reason : uint8_t {
  REASON_DRY = 1 << 0,       // humidity below 50 %
  REASON_HAZE = 1 << 1,      // PM2.5 above 50 ug/m3
  REASON_SMOKE = 1 << 2,     // PM2.5 above 150 ug/m3
  REASON_HOT = 1 << 3,       // temperature above 50 C
  REASON_VERY_HOT = 1 << 4,  // temperature above 80 C
  REASON_NO_DATA = 1 << 5,   // no valid sensor reading to evaluate
};

// Use NAN for a sensor without a current valid reading; it is skipped.
struct Readings {
  float maxTempC = NAN;  // hottest valid temperature from any sensor
  float humidityPct = NAN;
  float pm25 = NAN;
};

struct Assessment {
  Level level = Level::Normal;
  uint8_t reasons = 0;
};

inline Level maxLevel(Level a, Level b) { return a > b ? a : b; }

inline Assessment assess(const Readings &r) {
  Assessment result;
  bool anyData = false;

  if (!isnan(r.maxTempC)) {
    anyData = true;
    if (r.maxTempC > RESPONSE_TEMP_C) {
      result.level = maxLevel(result.level, Level::Response);
      result.reasons |= REASON_VERY_HOT | REASON_HOT;
    } else if (r.maxTempC > FIRE_TEMP_C) {
      result.level = maxLevel(result.level, Level::Fire);
      result.reasons |= REASON_HOT;
    }
  }

  if (!isnan(r.pm25)) {
    anyData = true;
    if (r.pm25 > PM25_FIRE_UGM3) {
      result.level = maxLevel(result.level, Level::Fire);
      result.reasons |= REASON_SMOKE | REASON_HAZE;
    } else if (r.pm25 > PM25_SURVEILLANCE_UGM3) {
      result.level = maxLevel(result.level, Level::Surveillance);
      result.reasons |= REASON_HAZE;
    }
  }

  // Dry air raises fire risk but does not indicate a fire by itself.
  if (!isnan(r.humidityPct)) {
    anyData = true;
    if (r.humidityPct < DRY_HUMIDITY_PCT) {
      result.level = maxLevel(result.level, Level::Surveillance);
      result.reasons |= REASON_DRY;
    }
  }

  if (!anyData) result.reasons |= REASON_NO_DATA;
  return result;
}

// Confirms a level only after it (or a higher one) has been seen in
// CONFIRM_COUNT consecutive assessments, so a single glitched reading does
// not raise an alarm. The confirmed level is the lowest level among the most
// recent CONFIRM_COUNT assessments, so it drops as soon as readings recover.
class Detector {
 public:
  static constexpr uint8_t CONFIRM_COUNT = 3;

  Level update(Level raw) {
    history_[next_] = raw;
    next_ = (next_ + 1) % CONFIRM_COUNT;
    if (filled_ < CONFIRM_COUNT) filled_++;

    Level confirmed = raw;
    for (uint8_t i = 0; i < filled_; i++) {
      if (history_[i] < confirmed) confirmed = history_[i];
    }
    // Until enough assessments exist, nothing above Normal is confirmed.
    confirmed_ = filled_ < CONFIRM_COUNT ? Level::Normal : confirmed;
    return confirmed_;
  }

  Level confirmed() const { return confirmed_; }

 private:
  Level history_[CONFIRM_COUNT] = {};
  uint8_t next_ = 0;
  uint8_t filled_ = 0;
  Level confirmed_ = Level::Normal;
};

inline const char *levelName(Level level) {
  switch (level) {
    case Level::Normal: return "NORMAL";
    case Level::Surveillance: return "SURVEILLANCE";
    case Level::Fire: return "FIRE";
    case Level::Response: return "FIRE >80C";
  }
  return "?";
}

}  // namespace fire
