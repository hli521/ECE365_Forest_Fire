#pragma once
// Experimental hybrid threshold + sliding-window wildfire detector.
// No Arduino dependencies. Call update() once for each NEW sensor sample.
// Thresholds are starting hypotheses, NOT validated fire-safety limits.
#include <math.h>
#include <stdint.h>
#include "fire_detection.h"

namespace hybrid_fire {

struct Config {
  float tempAbsoluteC = 50.0f;
  float tempRiseC = 8.0f;
  float pmAbsoluteUgM3 = 150.0f;
  float pmRiseUgM3 = 50.0f;
  float criticalTempC = 80.0f;
  float humidityReliableBelowPct = 85.0f;
  uint8_t minHistory = 5;
};

enum class Mode : uint8_t { ReliablePM, TemperatureOnly };

struct Result {
  fire::Level level = fire::Level::Normal;
  Mode mode = Mode::TemperatureOnly;
  bool humidityKnown = false;
  bool temperatureAnomaly = false;
  bool pmAnomaly = false;
  bool criticalTemperature = false;
  bool warmingUpTemperature = true;
  bool warmingUpPM = true;
  float temperatureBaseline = NAN;
  float pmBaseline = NAN;
  float temperatureDeviation = NAN;
  float pmDeviation = NAN;
  uint8_t reasons = 0;
};

template <uint8_t Capacity = 30> class Detector {
  static_assert(Capacity > 0, "History capacity must be positive");
  struct Window {
    float samples[Capacity] = {};
    uint8_t count = 0;
    uint8_t next = 0;
    float sum = 0.0f;
    float mean() const { return count ? sum / count : NAN; }
    void push(float v) {
      if (count == Capacity) sum -= samples[next];
      else ++count;
      samples[next] = v;
      sum += v;
      next = static_cast<uint8_t>((next + 1) % Capacity);
    }
  };

 public:
  explicit Detector(Config config = Config()) : config_(config) {}
  void reset() { temperature_ = Window(); pm_ = Window(); }
  Result update(const fire::Readings &r) {
    Result out;
    const bool tValid = isfinite(r.maxTempC) && r.maxTempC >= -40.0f && r.maxTempC <= 380.0f;
    const bool hValid = isfinite(r.humidityPct) && r.humidityPct >= 0 && r.humidityPct <= 100;
    const bool pValid = isfinite(r.pm25) && r.pm25 >= 0;
    out.humidityKnown = hValid;
    // Unknown RH => fail closed on PM reliability, NOT on temperature safety.
    out.mode = hValid && r.humidityPct < config_.humidityReliableBelowPct
                   ? Mode::ReliablePM : Mode::TemperatureOnly;
    const uint8_t needed = config_.minHistory > Capacity ? Capacity : config_.minHistory;
    out.temperatureBaseline = temperature_.mean();
    out.pmBaseline = pm_.mean();
    out.warmingUpTemperature = temperature_.count < needed;
    out.warmingUpPM = pm_.count < needed;
    if (tValid) {
      out.temperatureDeviation = r.maxTempC - out.temperatureBaseline;
      out.criticalTemperature = r.maxTempC >= config_.criticalTempC;
      out.temperatureAnomaly = out.criticalTemperature ||
          (!out.warmingUpTemperature && r.maxTempC > config_.tempAbsoluteC &&
           out.temperatureDeviation > config_.tempRiseC);
      if (out.temperatureAnomaly) out.reasons |= fire::REASON_HOT;
      if (out.criticalTemperature) out.reasons |= fire::REASON_VERY_HOT;
    }
    if (pValid && out.mode == Mode::ReliablePM) {
      out.pmDeviation = r.pm25 - out.pmBaseline;
      out.pmAnomaly = !out.warmingUpPM && r.pm25 > config_.pmAbsoluteUgM3 &&
                      out.pmDeviation > config_.pmRiseUgM3;
      if (out.pmAnomaly) out.reasons |= fire::REASON_SMOKE;
    }
    if (!tValid && !pValid) out.reasons |= fire::REASON_NO_DATA;
    if (out.criticalTemperature) out.level = fire::Level::Response;
    else if (out.temperatureAnomaly && out.pmAnomaly && out.mode == Mode::ReliablePM)
      out.level = fire::Level::Fire;
    else if (out.temperatureAnomaly || out.pmAnomaly)
      out.level = fire::Level::Surveillance;

    // Update AFTER assessment: current sample must not influence its own baseline.
    // Freeze on an anomaly to prevent a developing fire from shifting the baseline.
    if (tValid && !out.temperatureAnomaly) temperature_.push(r.maxTempC);
    if (pValid && out.mode == Mode::ReliablePM && !out.pmAnomaly) pm_.push(r.pm25);
    return out;
  }
 private:
  Config config_;
  Window temperature_;
  Window pm_;
};

inline const char *modeName(Mode m) {
  return m == Mode::ReliablePM ? "PM_RELIABLE" : "TEMPERATURE_ONLY";
}
} // namespace hybrid_fire
