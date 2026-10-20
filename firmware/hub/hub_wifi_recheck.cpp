#include "hub_wifi_recheck.h"
#ifdef PPE_S3_WIFI_RECHECK
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "hub_ble.h"
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"       // git-ignored: WIFI_SSID, WIFI_PASS, GATEWAY_HOST, GATEWAY_PORT
#else
#error "Copy firmware/hub/wifi_secrets.example.h to firmware/hub/wifi_secrets.h and fill it in (the real file is git-ignored)"
#endif

static void wifi_task(void*) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) vTaskDelay(pdMS_TO_TICKS(250));
  Serial.printf("WIFI status=%d ip=%s rssi=%d\n", (int)WiFi.status(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(30000));
    if (WiFi.status() != WL_CONNECTED) { Serial.println("WIFI not connected, request skipped"); continue; }
    HTTPClient http;
    String url = String("http://") + GATEWAY_HOST + ":" + String(GATEWAY_PORT) + "/s3recheck";
    char body[96];
    snprintf(body, sizeof body, "{\"src\":\"hub\",\"t_ms\":%lu,\"rx_total\":%lu}", (unsigned long)millis(),
             (unsigned long)hub_pipeline().global().rx_total);
    http.setTimeout(5000);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    uint32_t a = millis();
    int code = http.POST((uint8_t*)body, strlen(body));
    Serial.printf("WIFI http code=%d took=%lu ms rssi=%d\n", code, (unsigned long)(millis() - a), (int)WiFi.RSSI());
    http.end();
  }
}

void hub_wifi_recheck_start() { xTaskCreate(wifi_task, "ppe_wifi", 8192, nullptr, 2, nullptr); }
#endif
