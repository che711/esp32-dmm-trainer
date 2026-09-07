// SPDX-License-Identifier: MIT
#include "config.h"
#include "faults.h"
#include "hal/hardware.h"
#include <esp_random.h>

FaultTrainer faults;

static const FaultCase kCases[] = {
    {"open_wire",
     "Обрыв жилы в жгуте",
     "Цепь между клеммами R+ и COM не работает. Питание есть.",
     "Прозвонка BeepLit: OL вместо писка. Барграф помогает найти надлом при шевелении."},
    {"ghost_voltage",
     "Фантомное напряжение вместо настоящего",
     "На клемме G+ вольтметр показывает напряжение, но нагрузка не работает.",
     "AutoV/LoZ: в LoZ показание падает почти до нуля — значит, это наводка."},
    {"short_to_com",
     "Короткое замыкание на общий провод",
     "Источник уходит в защиту сразу после включения нагрузки.",
     "Прозвонка на обесточенной цепи: писк там, где его быть не должно."},
    {"bad_diode",
     "Пробитый диод",
     "Выпрямитель греется, на выходе нет постоянного напряжения.",
     "Диодный тест: 0,000 В и непрерывный писк в обе стороны."},
    {"degraded_cap",
     "Усохший электролитический конденсатор",
     "Схема работает нестабильно, на питании слышен «звон».",
     "Режим ёмкости: измеренное значение в разы меньше маркировки."},
    {"chattering_contact",
     "Дребезжащий контакт",
     "Нагрузка периодически моргает, при постукивании по стенду — сильнее.",
     "Прозвонка + барграф: цифры не успевают, а барграф ловит микроразрывы. AutoHold."},
    {"low_reference",
     "Просевшее опорное напряжение",
     "Все измерения канала уходят на несколько процентов от паспортных.",
     "DCV на пределе 6 В против эталонных 2,500 В: отклонение больше 0,03% + 2 ед."},
    {"loop_out_of_range",
     "Датчик петли 4-20 мА вышел за диапазон",
     "Система управления сообщает об отказе датчика.",
     "Режим %4-20 мА: ток ниже 4 мА (обрыв петли) или выше 20 мА."},
};

uint8_t FaultTrainer::caseCount() { return sizeof(kCases) / sizeof(kCases[0]); }

void FaultTrainer::begin() { stop(); }

const FaultCase *FaultTrainer::current() const {
  return (_index >= 0 && _index < (int8_t)caseCount()) ? &kCases[_index] : nullptr;
}

const char *FaultTrainer::symptom() const {
  const FaultCase *c = current();
  return c ? c->symptom : "";
}

void FaultTrainer::apply(uint8_t index) {
  hal::Board &b = hal::board;
  b.safeState();
  const char *id = kCases[index].id;

  if (!strcmp(id, "open_wire")) {
    b.decade.select(7); // канал «open»
  } else if (!strcmp(id, "ghost_voltage")) {
    b.ghost.enable(true);
  } else if (!strcmp(id, "short_to_com")) {
    b.decade.select(0); // 1 Ом ~ закоротка
  } else if (!strcmp(id, "bad_diode")) {
    b.zoo.select(3); // пробитый 1N4148
  } else if (!strcmp(id, "degraded_cap")) {
    b.zoo.select(7); // «1000 мкФ», реально 220
  } else if (!strcmp(id, "chattering_contact")) {
    b.decade.select(0);
    b.chatter.configure(700, 8);
    b.chatter.enable(true);
  } else if (!strcmp(id, "low_reference")) {
    b.reference.select(0);
    b.level.setMillivolts(2350.0f); // вместо 2500 мВ
  } else if (!strcmp(id, "loop_out_of_range")) {
    b.loop.setMilliamps(2.0f); // обрыв петли
  }
}

bool FaultTrainer::start(const char *id) {
  int8_t idx = -1;
  if (id && *id) {
    for (uint8_t i = 0; i < caseCount(); i++)
      if (!strcmp(kCases[i].id, id)) { idx = i; break; }
    if (idx < 0) return false;
  } else {
    idx = (int8_t)(esp_random() % caseCount());
  }
  _index = idx;
  _active = true;
  _solved = false;
  _attempts = 0;
  apply(idx);
  Serial.println("[faults] случай запущен (id скрыт)");
  return true;
}

void FaultTrainer::stop() {
  _active = false;
  _solved = false;
  _index = -1;
  _attempts = 0;
  hal::board.safeState();
}

bool FaultTrainer::answer(const char *id) {
  if (!_active || !id) return false;
  _attempts++;
  const FaultCase *c = current();
  if (c && !strcmp(c->id, id)) {
    _solved = true;
    return true;
  }
  return false;
}

void FaultTrainer::describeCases(JsonArray out) const {
  for (uint8_t i = 0; i < caseCount(); i++) {
    JsonObject o = out.add<JsonObject>();
    o["id"] = kCases[i].id;
    // Названия нужны для выпадающего списка ответов; симптом и метод
    // отдаются только после разгадки — иначе задание теряет смысл.
    o["title"] = kCases[i].title;
  }
}
