// ppe_batt.h - battery percent for the packet battery byte (SRS FR-13, protocol 6.1, state machines T-03). Pure logic.
// The voltage-to-percent table is a PROPOSAL (one Li-ion cell, piecewise linear, 3.3 V = 0 %, 4.2 V = 100 %); no frozen
// document gives one, the owner decides (S4 report). This board has no battery divider (A-26), so a real read is not
// available and reports 0xFF; TC-13 runs on injected millivolts (batt hook, A-17, P5).
#pragma once
#include <stdint.h>

typedef enum {
  PPE_BATT_SRC_NONE = 0,     // no battery input on this board: 0xFF (not available)
  PPE_BATT_SRC_INJECTED,     // test hook "batt <mV>"
  PPE_BATT_SRC_FAIL          // test hook "batt fail", or a failed ADC read: 0xFF
} ppe_batt_src_t;

// 0..100, non-increasing as mv falls, clamped outside 3300..4200 mV.
uint8_t ppe_batt_pct_from_mv(uint16_t mv);
// The battery byte for the next packet: the percent, or PPE_BATT_NA (0xFF) when the reading is unavailable or failed.
uint8_t ppe_batt_report(ppe_batt_src_t src, uint16_t mv);
