#include "ppe_batt.h"
#include "ppe_proto.h"

namespace {
struct Pt { uint16_t mv; uint8_t pct; };
// PROPOSED table (see ppe_batt.h), falling voltage order.
const Pt TABLE[] = {{4200, 100}, {4100, 90}, {4000, 80}, {3900, 70}, {3800, 55}, {3700, 40},
                    {3600, 25},  {3500, 15}, {3400, 8},  {3300, 0}};
const unsigned N = sizeof TABLE / sizeof TABLE[0];
}

uint8_t ppe_batt_pct_from_mv(uint16_t mv) {
  if (mv >= TABLE[0].mv) return TABLE[0].pct;
  for (unsigned i = 1; i < N; i++) {
    if (mv >= TABLE[i].mv) {             // between TABLE[i] (lower) and TABLE[i-1] (upper): linear, rounded down
      const Pt& lo = TABLE[i]; const Pt& hi = TABLE[i - 1];
      return (uint8_t)(lo.pct + (uint32_t)(hi.pct - lo.pct) * (uint32_t)(mv - lo.mv) / (uint32_t)(hi.mv - lo.mv));
    }
  }
  return TABLE[N - 1].pct;
}

uint8_t ppe_batt_report(ppe_batt_src_t src, uint16_t mv) {
  return src == PPE_BATT_SRC_INJECTED ? ppe_batt_pct_from_mv(mv) : PPE_BATT_NA;
}
