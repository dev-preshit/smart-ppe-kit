#include "ppe_sense.h"

ppe_raw_t ppe_conv_glove(bool connected) { return ppe_raw_make(connected ? PPE_WEAR_WORN : PPE_WEAR_NOT_WORN, 0); }

ppe_raw_t ppe_conv_helmet(bool crown_closed, bool strap_closed) {
  return ppe_raw_make((crown_closed && strap_closed) ? PPE_WEAR_WORN : PPE_WEAR_NOT_WORN, 0);
}

uint16_t ppe_fsr_mean(const uint16_t* s, unsigned n) {
  if (!s || n == 0) return 0;
  uint32_t sum = 0;
  for (unsigned i = 0; i < n; i++) sum += s[i];
  return (uint16_t)((sum + n / 2) / n);
}

void ppe_fsr_init(ppe_fsr_t* f) { f->have_prev = false; f->prev = PPE_WEAR_NOT_WORN; }
void ppe_fsr_seed(ppe_fsr_t* f, ppe_wear_t prev) {
  f->have_prev = (prev == PPE_WEAR_WORN || prev == PPE_WEAR_NOT_WORN);
  f->prev = f->have_prev ? prev : PPE_WEAR_NOT_WORN;
}

ppe_raw_t ppe_conv_fsr(ppe_fsr_t* f, bool adc_ok, uint32_t count) {
  if (!adc_ok || count > PPE_ADC_MAX) {                       // R-4
    f->have_prev = true; f->prev = PPE_WEAR_NOT_WORN;
    return ppe_raw_make(PPE_WEAR_NOT_WORN, 1);
  }
  if (count >= PPE_FSR_WORN_MIN)          f->prev = PPE_WEAR_WORN;
  else if (count <= PPE_FSR_NOT_WORN_MAX) f->prev = PPE_WEAR_NOT_WORN;
  else if (!f->have_prev)                 f->prev = PPE_WEAR_NOT_WORN;   // R-5: dead band, no history
  f->have_prev = true;                                         // dead band otherwise keeps f->prev (R-2)
  return ppe_raw_make(f->prev, 0);                             // R-6: a valid reading clears the fault
}
