// History for the Graph screen. No hardware calls: testable on the PC.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace thermo {

static const int kGraphPoints = 288;  // one per screen column
static const int16_t kGraphNone = INT16_MIN;

// Tenths of a degree F; kGraphNone if unknown.
struct GraphPoint {
  int16_t avg = kGraphNone, min = kGraphNone, max = kGraphNone, set = kGraphNone;
};

struct GraphSpan {
  uint32_t start = 0;   // UTC, a multiple of bucket
  uint32_t bucket = 0;  // seconds per point
  uint32_t end() const { return start + kGraphPoints * bucket; }
};

// The span of kGraphPoints whose last point holds `now`.
GraphSpan graphSpan(uint32_t now, uint32_t bucket);

// Reads the server's reply: a line "graph 1", then lines "time,avg,min,max,set" in tenths, with
// empty temperatures if there was none. Points outside the span are ignored. False if it is not
// such a reply. `out` has kGraphPoints entries.
bool parseGraph(const char* text, const GraphSpan& span, GraphPoint* out);

// Vertical scale, in tenths: at least 4 degrees high, and at most 5 lines, at multiples of step.
// False if there is nothing to show.
bool graphScale(const GraphPoint* points, int& lo, int& hi, int& step);

}  // namespace thermo
