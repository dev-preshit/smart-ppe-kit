// Wi-Fi + BLE recheck 1 (sprint S3, SRS R-04 / AS-09). Bench only: compiled only with -DPPE_S3_WIFI_RECHECK.
// The hub joins the owner's Wi-Fi and sends one small HTTP POST to tools/gateway_stub.py every 30 s while the scan runs.
// The real uplink (queue, retry, token) is sprint S7; credentials never go into the repo (wifi_secrets.h is git-ignored).
#pragma once
#ifdef PPE_S3_WIFI_RECHECK
void hub_wifi_recheck_start();
#endif
