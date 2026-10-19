// S3 bench tool: BLE advertisement injector (spare ESP32-C3). TEST TOOL, never part of a release.
// Board: "ESP32C3 Dev Module", USB CDC On Boot: Enabled. build_opt.h next to this file holds -DPPE_TEST_HOOKS.
// Serial 115200, one command per line (commanded from the PC by tools/inject.py):
//   ADV <hex> [copies] [spacing_ms]   send the bytes as a NON-connectable advertisement, exactly as given
//                                     (any length 0..31, malformed headers included). Each copy is one burst of
//                                     ADV_BURST_MS (20-30 ms interval); copies start spacing_ms apart (default 500).
//   PING                              reply PONG
// Replies: "OK seq=N len=L copies=K" when it starts, "DONE seq=N" when the last burst ended, "ERR ..." on bad input.
// Never run this at the same time as another program on the same COM port.
#include <NimBLEDevice.h>
#include <ppe_common.h>

#define ADV_BURST_MS      100u    // one transmission "copy" = a 100 ms burst (spec 4: PPE_ADV_BURST_MS_DEFAULT)
#define DEFAULT_SPACING   500u    // PPE_RETX_PERIOD_MS
#define MAX_COPIES        50u
#define MAX_ADV_BYTES     31u

static char line[256];
static size_t lineLen = 0;
static uint32_t seq = 0;
static NimBLEAdvertising* advertising = nullptr;

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool parseHex(const char* s, uint8_t* out, size_t max, size_t* n, const char** end) {
  size_t k = 0; int hi = -1;
  for (; *s && *s != ' ' && *s != '\t'; s++) {
    int v = hexval(*s);
    if (v < 0) return false;
    if (hi < 0) hi = v;
    else { if (k >= max) return false; out[k++] = (uint8_t)((hi << 4) | v); hi = -1; }
  }
  if (hi >= 0) return false;
  *n = k; *end = s;
  return true;
}

static void sendCopy(const uint8_t* data, size_t len) {
  NimBLEAdvertisementData ad;
  if (len) ad.addData(data, len);                 // raw bytes, nothing added or fixed by the library
  advertising->setAdvertisementData(ad);
  advertising->enableScanResponse(false);
  advertising->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  advertising->setMinInterval(0x20);              // 20 ms
  advertising->setMaxInterval(0x30);              // 30 ms
  if (!advertising->start(ADV_BURST_MS)) Serial.printf("ERR seq=%lu advertising start failed\n", (unsigned long)seq);
  delay(ADV_BURST_MS + 10);
  advertising->stop();
}

static void handleAdv(const char* args) {
  while (*args == ' ') args++;
  uint8_t data[MAX_ADV_BYTES]; size_t len = 0; const char* rest = args;
  if (!parseHex(args, data, MAX_ADV_BYTES, &len, &rest)) { Serial.println("ERR bad hex or longer than 31 bytes"); return; }
  unsigned long copies = 1, spacing = DEFAULT_SPACING;
  while (*rest == ' ') rest++;
  if (*rest) { copies = strtoul(rest, (char**)&rest, 10); while (*rest == ' ') rest++; }
  if (*rest) spacing = strtoul(rest, (char**)&rest, 10);
  if (copies < 1 || copies > MAX_COPIES || spacing < ADV_BURST_MS + 20) { Serial.println("ERR copies 1..50, spacing >= 120 ms"); return; }
  seq++;
  Serial.printf("OK seq=%lu len=%u copies=%lu spacing_ms=%lu\n", (unsigned long)seq, (unsigned)len, copies, spacing);
  uint32_t t0 = millis();
  for (unsigned long i = 0; i < copies; i++) {
    uint32_t target = t0 + (uint32_t)(i * spacing);
    while ((int32_t)(millis() - target) < 0) delay(1);
    sendCopy(data, len);
  }
  Serial.printf("DONE seq=%lu\n", (unsigned long)seq);
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(200);
  NimBLEDevice::init("");
  NimBLEDevice::setPower(3);
  advertising = NimBLEDevice::getAdvertising();
  Serial.println("INJECTOR ready");
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLen] = 0; lineLen = 0;
      if (!strncmp(line, "ADV", 3) && (line[3] == ' ' || line[3] == 0)) handleAdv(line + 3);
      else if (!strcmp(line, "PING")) Serial.println("PONG");
      else if (line[0]) Serial.println("ERR unknown command (ADV <hex> [copies] [spacing_ms] | PING)");
    } else if (lineLen < sizeof line - 1) line[lineLen++] = c;
    else lineLen = 0;
  }
  delay(1);
}
