#include <cassert>
#include <cstdio>
#include "hybrid_fire_detection.h"
int main() {
  hybrid_fire::Detector<> d;
  fire::Readings r;
  r.maxTempC=30;r.pm25=20;r.humidityPct=70;
  for(int i=0;i<5;i++) assert(d.update(r).level==fire::Level::Normal);
  r.maxTempC=53;r.pm25=180;
  auto a=d.update(r);
  assert(a.level==fire::Level::Fire && a.temperatureAnomaly && a.pmAnomaly);
  r.humidityPct=96;
  a=d.update(r);
  assert(a.level==fire::Level::Surveillance && !a.pmAnomaly && a.temperatureAnomaly);
  r.maxTempC=85;
  a=d.update(r);
  assert(a.level==fire::Level::Response);
  r.maxTempC=NAN;r.pm25=NAN;r.humidityPct=NAN;
  a=d.update(r);
  assert(a.level==fire::Level::Normal && (a.reasons & fire::REASON_NO_DATA));
  puts("PASS: device-side hybrid detector");
}
