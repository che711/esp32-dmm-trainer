// SPDX-License-Identifier: MIT
#include "config.h"
#include "scenario.h"
#include "hal/hardware.h"
#include <LittleFS.h>
#include <math.h>

ScenarioEngine scenario;

static void copyStr(char *dst, size_t n, const char *src) {
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

bool ScenarioEngine::loadFromFile(const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  _doc.clear();
  DeserializationError err = deserializeJson(_doc, f);
  f.close();
  if (err) {
    Serial.printf("[scenario] parse error: %s\n", err.c_str());
    return false;
  }
  copyStr(_id, sizeof(_id), _doc["id"] | "");
  copyStr(_title, sizeof(_title), _doc["title"] | "");
  _steps = _doc["steps"].as<JsonArray>();
  return !_steps.isNull() && _steps.size() > 0;
}

bool ScenarioEngine::start(const char *id) {
  char path[64];
  snprintf(path, sizeof(path), "/scenarios/%s.json", id);
  if (!loadFromFile(path)) return false;

  hal::board.safeState();
  clearTruth();
  _state = RunState::Running;
  _startMs = millis();
  _step = 0;
  _nextTruthAt = _startMs;
  applyStep(_steps[0]);
  Serial.printf("[scenario] start %s (%u steps)\n", _id, (unsigned)_steps.size());
  return true;
}

void ScenarioEngine::stop() {
  if (_state == RunState::Running) Serial.println("[scenario] stop");
  _state = RunState::Idle;
  _truth = NAN;
  _quantity[0] = 0;
  hal::board.safeState();
}

uint32_t ScenarioEngine::elapsedMs() const {
  return _state == RunState::Running ? millis() - _startMs : 0;
}

const TruthSample &ScenarioEngine::truthAt(size_t i) const {
  static TruthSample empty{};
  return i < _count ? _log[i] : empty;
}

void ScenarioEngine::clearTruth() { _count = 0; }

void ScenarioEngine::recordTruth() {
  if (_count >= TRUTH_BUFFER_SAMPLES) return; // буфер полон — дальше не пишем
  TruthSample &s = _log[_count++];
  s.tMs = elapsedMs();
  s.stepIndex = _step;
  s.value = _truth;
  copyStr(s.quantity, sizeof(s.quantity), _quantity);
}

void ScenarioEngine::applyStep(JsonObjectConst step) {
  hal::Board &b = hal::board;

  // --- выставляем железо -------------------------------------------------
  if (step["reference_tap"].is<int>()) b.reference.select(step["reference_tap"].as<int>());
  if (step["level_mv"].is<float>()) b.level.setMillivolts(step["level_mv"].as<float>());
  if (step["decade"].is<int>()) b.decade.select(step["decade"].as<int>());
  if (step["component"].is<int>()) b.zoo.select(step["component"].as<int>());
  if (step["ghost"].is<bool>()) b.ghost.enable(step["ghost"].as<bool>());
  if (step["chatter"].is<bool>()) b.chatter.enable(step["chatter"].as<bool>());
  if (step["heater"].is<int>()) b.thermal.setHeater(step["heater"].as<int>());
  if (step["fan"].is<bool>()) b.thermal.setFan(step["fan"].as<bool>());
  if (step["loop_percent"].is<float>()) b.loop.setPercent(step["loop_percent"].as<float>());
  if (step["inrush_ms"].is<int>()) b.inrush.pulse(step["inrush_ms"].as<int>());

  if (step["loads"].is<JsonObjectConst>()) {
    JsonObjectConst l = step["loads"];
    b.loads.set(l["led"] | false, l["mid"] | false, l["high"] | false);
  }

  if (step["signal"].is<JsonObjectConst>()) {
    JsonObjectConst sg = step["signal"];
    b.signal.set(sg["hz"] | LEDC_DEFAULT_HZ, sg["duty"] | 50.0f);
    b.signal.enable(sg["on"] | true);
  } else if (step["signal_off"] | false) {
    b.signal.enable(false);
  }

  // --- определяем ожидаемое значение -------------------------------------
  copyStr(_quantity, sizeof(_quantity), step["quantity"] | "");
  copyStr(_hint, sizeof(_hint), step["hint"] | "");

  if (step["truth"].is<float>()) {
    _truth = step["truth"].as<float>(); // значение задано явно в сценарии
  } else {
    // ...или вычисляется из состояния железа — так оно всегда согласовано
    const String q(_quantity);
    if (q == "dcv") {
      _truth = b.reference.tap() ? b.reference.millivolts() / 1000.0f
             : b.signal.enabled() ? b.signal.expectedDcMv() / 1000.0f
                                  : b.level.millivolts() / 1000.0f;
    } else if (q == "dcmv") {
      _truth = b.reference.tap() ? b.reference.millivolts() : b.level.millivolts();
    } else if (q == "acv") {
      _truth = b.signal.expectedAcRmsMv() / 1000.0f;
    } else if (q == "acdcv") {
      _truth = b.signal.expectedAcDcRmsMv() / 1000.0f;
    } else if (q == "ohm") {
      _truth = b.decade.ohms();
    } else if (q == "hz") {
      _truth = (float)b.signal.freqHz();
    } else if (q == "duty") {
      _truth = b.signal.duty();
    } else if (q == "ma") {
      _truth = b.loads.expectedCurrentMa();
    } else if (q == "loop_ma") {
      _truth = b.loop.milliamps();
    } else if (q == "percent") {
      _truth = b.loop.percent();
    } else if (q == "degc") {
      _truth = b.thermal.readReferenceC();
    } else {
      _truth = NAN;
    }
  }

  const uint32_t dur = step["ms"] | 10000;
  _stepEndsAt = millis() + dur;
  recordTruth();
}

void ScenarioEngine::tick() {
  hal::board.tick();
  if (_state != RunState::Running) return;

  const uint32_t now = millis();

  if ((int32_t)(now - _nextTruthAt) >= 0) {
    _nextTruthAt = now + TRUTH_PERIOD_MS;
    // Температура меняется непрерывно — обновляем истину на лету
    if (strcmp(_quantity, "degc") == 0) _truth = hal::board.thermal.readReferenceC();
    recordTruth();
  }

  if ((int32_t)(now - _stepEndsAt) >= 0) {
    if (_step + 1 < _steps.size()) {
      _step++;
      applyStep(_steps[_step]);
    } else {
      _state = RunState::Finished;
      hal::board.safeState();
      Serial.printf("[scenario] finished, %u truth samples\n", (unsigned)_count);
    }
  }
}

void ScenarioEngine::listScenarios(JsonArray out) {
  File dir = LittleFS.open("/scenarios");
  if (!dir || !dir.isDirectory()) return;
  File f = dir.openNextFile();
  while (f) {
    String name(f.name());
    if (name.endsWith(".json")) {
      JsonDocument doc;
      if (deserializeJson(doc, f) == DeserializationError::Ok) {
        JsonObject o = out.add<JsonObject>();
        o["id"] = doc["id"] | "";
        o["title"] = doc["title"] | "";
        o["function"] = doc["function"] | "";
        o["steps"] = doc["steps"].size();
      }
    }
    f = dir.openNextFile();
  }
}
