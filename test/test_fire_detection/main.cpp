#include <assert.h>
#include "fire_detection.h"
int main() {
  using namespace fire;
  Readings r;
  assert(assess(r).reasons == REASON_NO_DATA);
  r.vocIndex = 0; r.noxIndex = 501;
  assert(assess(r).reasons == REASON_NO_DATA);
  r.vocIndex = VOC_SURVEILLANCE_INDEX; r.noxIndex = NOX_SURVEILLANCE_INDEX;
  assert(assess(r).level == Level::Normal);
  r.vocIndex += 0.1f;
  assert(assess(r).level == Level::Surveillance);
  assert(assess(r).reasons == REASON_VOC);
  r.vocIndex = NAN; r.noxIndex += 0.1f;
  assert(assess(r).level == Level::Surveillance);
  assert(assess(r).reasons == REASON_NOX);
  r.vocIndex = 500; r.noxIndex = 500; r.humidityPct = 70; r.pm25 = 0;
  assert(assess(r).level == Level::Surveillance); // Gases alone never mean fire.
  r.maxTempC = 50;
  assert(assess(r).level == Level::Surveillance);
  r.maxTempC = 50.1f;
  assert(assess(r).level == Level::Fire);
  r.maxTempC = 80.1f;
  assert(assess(r).level == Level::Response);
  r.maxTempC = NAN; r.pm25 = 151;
  assert(assess(r).level == Level::Fire);
  r = Readings{}; r.humidityPct = 40; r.pm25 = 0;
  assert(assess(r).level == Level::Normal); // Preserve existing AND rule.
  r.pm25 = 51;
  assert(assess(r).level == Level::Surveillance);
  Detector detector;
  assert(detector.update(Level::Surveillance) == Level::Normal);
  assert(detector.update(Level::Surveillance) == Level::Normal);
  assert(detector.update(Level::Surveillance) == Level::Surveillance);
  assert(detector.update(Level::Normal) == Level::Normal);
  char reasons[64];
  describeReasons(REASON_VOC | REASON_NOX, reasons, sizeof(reasons));
  assert(strstr(reasons, "VOC-high") && strstr(reasons, "NOx-high"));
}
