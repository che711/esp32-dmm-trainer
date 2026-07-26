// SPDX-License-Identifier: MIT
// Режим «найди неисправность»: стенд вносит один скрытый дефект и молчит,
// пока пользователь не поставит диагноз.

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct FaultCase {
  const char *id;
  const char *title;   // показывается только при разборе
  const char *symptom; // что видит обучаемый до измерений
  const char *method;  // каким режимом мультиметра ищется
};

class FaultTrainer {
public:
  void begin();
  // Запускает случайный случай (или конкретный, если id != nullptr)
  bool start(const char *id = nullptr);
  void stop();

  bool active() const { return _active; }
  bool solved() const { return _solved; }
  uint8_t attempts() const { return _attempts; }
  const FaultCase *current() const;
  const char *symptom() const;

  // Проверка ответа: сравнивается id случая
  bool answer(const char *id);

  void describeCases(JsonArray out) const;
  static uint8_t caseCount();

private:
  void apply(uint8_t index);
  int8_t _index = -1;
  bool _active = false;
  bool _solved = false;
  uint8_t _attempts = 0;
};

extern FaultTrainer faults;
