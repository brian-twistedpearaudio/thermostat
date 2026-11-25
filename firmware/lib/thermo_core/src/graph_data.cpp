#include "graph_data.h"

#include <stdlib.h>
#include <string.h>

#include <initializer_list>

namespace thermo {

GraphSpan graphSpan(uint32_t now, uint32_t bucket) {
  GraphSpan s;
  s.bucket = bucket;
  s.start = now / bucket * bucket + bucket - kGraphPoints * bucket;
  return s;
}

// One number, or nothing, up to `stop`. Moves p past the stop.
static bool field(const char*& p, char stop, bool mayBeEmpty, long& value, bool& empty) {
  empty = *p == stop;
  if (empty) {
    ++p;
    return mayBeEmpty;
  }
  if (*p != '-' && (*p < '0' || *p > '9')) return false;
  char* end;
  value = strtol(p, &end, 10);
  if (end == p || *end != stop) return false;
  p = end + 1;
  return true;
}

bool parseGraph(const char* text, const GraphSpan& span, GraphPoint* out) {
  static const char kHead[] = "graph 1\n";
  if (strncmp(text, kHead, sizeof(kHead) - 1) != 0) return false;
  for (int i = 0; i < kGraphPoints; ++i) out[i] = GraphPoint();

  const char* p = text + sizeof(kHead) - 1;
  while (*p != '\0') {
    long time, v[4];
    bool none;
    if (!field(p, ',', false, time, none)) return false;
    GraphPoint point;
    int16_t* const to[4] = {&point.avg, &point.min, &point.max, &point.set};
    for (int i = 0; i < 4; ++i) {
      if (!field(p, i < 3 ? ',' : '\n', i < 3, v[i], none)) return false;
      if (none) continue;
      if (v[i] < -1000 || v[i] > 2000) return false;
      *to[i] = static_cast<int16_t>(v[i]);
    }
    const uint32_t t = static_cast<uint32_t>(time);
    if (time < 0 || t < span.start || t >= span.end()) continue;
    out[(t - span.start) / span.bucket] = point;
  }
  return true;
}

bool graphScale(const GraphPoint* points, int& lo, int& hi, int& step) {
  bool any = false;
  for (int i = 0; i < kGraphPoints; ++i) {
    for (int v : {points[i].avg, points[i].min, points[i].max, points[i].set}) {
      if (v == kGraphNone) continue;
      if (!any || v < lo) lo = v;
      if (!any || v > hi) hi = v;
      any = true;
    }
  }
  if (!any) return false;

  const int pad = hi - lo < 34 ? (40 - (hi - lo) + 1) / 2 : 3;
  lo -= pad;
  hi += pad;
  static const int kSteps[] = {10, 20, 50, 100, 200, 500, 1000};
  for (int s : kSteps) {
    step = s;
    if ((hi - lo) / s < 5) break;
  }
  return true;
}

}  // namespace thermo
