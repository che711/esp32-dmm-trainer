// SPDX-License-Identifier: MIT
// Движок сценариев: проигрывает список шагов из JSON, на каждом шаге
// выставляет железо и пишет в кольцевой буфер «истинное» значение,
// с которым потом сверяется CSV-лог мультиметра.

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct TruthSample {
  uint32_t tMs;      // мс от старта сценария
  uint16_t stepIndex;
  float value;       // ожидаемое значение в единицах quantity
  char quantity[12]; // dcv | acv | acdcv | ohm | ma | ua | percent | hz | duty | degc | farad
};

enum class RunState : uint8_t { Idle, Running, Finished };

class ScenarioEngine {
public:
  bool loadFromFile(const char *path);
  bool start(const char *id);
  void stop();
  void tick();

  RunState state() const { return _state; }
  const char *id() const { return _id; }
  const char *title() const { return _title; }
  uint16_t stepIndex() const { return _step; }
  uint16_t stepCount() const { return _steps.size(); }
  uint32_t elapsedMs() const;
  const char *currentQuantity() const { return _quantity; }
  float currentTruth() const { return _truth; }
  const char *currentHint() const { return _hint; }

  // Ground-truth лог
  size_t truthCount() const { return _count; }
  const TruthSample &truthAt(size_t i) const;
  void clearTruth();

  // Список доступных сценариев в каталоге /scenarios на LittleFS
  static void listScenarios(JsonArray out);

private:
  void applyStep(JsonObjectConst step);
  void recordTruth();

  JsonDocument _doc;
  JsonArray _steps;
  char _id[32] = {0};
  char _title[64] = {0};
  char _quantity[12] = {0};
  char _hint[96] = {0};
  float _truth = NAN;

  RunState _state = RunState::Idle;
  uint32_t _startMs = 0;
  uint32_t _stepEndsAt = 0;
  uint32_t _nextTruthAt = 0;
  uint16_t _step = 0;

  TruthSample _log[TRUTH_BUFFER_SAMPLES];
  size_t _count = 0;
};

extern ScenarioEngine scenario;
