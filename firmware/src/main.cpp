// SPDX-License-Identifier: MIT
// DMM Trainer — стенд для отработки функций мультиметра Brymen BM788BT.
//
// Стенд выставляет известные величины, ведёт ground-truth лог и отдаёт его
// в CSV. Логи мультиметра (BLE-Comm -> CSV) сверяются с ним пакетом tools/.
//
// ВНИМАНИЕ: стенд рассчитан ТОЛЬКО на безопасные напряжения (<= 24 В).
// Никогда не подключайте его к сети 230 В.

#include "config.h"
#include "faults.h"
#include "hal/hardware.h"
#include "scenario.h"
#include "webapi.h"
#include <LittleFS.h>
#include <WiFi.h>

static void startNetwork() {
  const char *staSsid = WIFI_STA_SSID;
  if (strlen(staSsid) > 0) {
    Serial.printf("[wifi] подключаюсь к %s\n", staSsid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(staSsid, WIFI_STA_PASS);
    const uint32_t deadline = millis() + WIFI_STA_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && (int32_t)(millis() - deadline) < 0) {
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[wifi] STA, адрес http://%s/\n", WiFi.localIP().toString().c_str());
      return;
    }
    Serial.println("[wifi] не удалось — поднимаю точку доступа");
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[wifi] AP \"%s\", адрес http://%s/\n", AP_SSID,
                WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nDMM Trainer %s\n", FW_VERSION);

  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS не смонтирован — залейте образ data/");
  }

  hal::board.begin();
  faults.begin();
  startNetwork();
  webapi::begin();

  digitalWrite(PIN_STATUS_LED, HIGH);
  Serial.println("Готов. Стенд в безопасном состоянии.");
}

void loop() {
  webapi::tick();
  scenario.tick();

  // Мигание статусным светодиодом: медленно — простой, быстро — идёт сценарий.
  static uint32_t next = 0;
  const uint32_t period = scenario.state() == RunState::Running ? 200 : 1500;
  if (millis() > next) {
    next = millis() + period;
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
  }
}
