// SPDX-License-Identifier: MIT
// Аппаратный слой стенда. Каждый класс отвечает за один физический модуль
// и умеет сообщать своё «истинное» значение для ground-truth лога.

#pragma once
#include <Arduino.h>

namespace hal {

// ------------------------------------------------------------ PCF8574
// Восемь выходов на I2C. Состояние хранится теневым регистром: PCF8574
// не читается побитово, писать всегда приходится целым байтом.
class Expander {
public:
  bool begin();
  void write(uint8_t bit, bool value);
  bool read(uint8_t bit) const { return _shadow & (1 << bit); }
  void writeAll(uint8_t mask);
  bool present() const { return _present; }

private:
  void flush();
  uint8_t _shadow = 0x00;
  bool _present = false;
};

// ------------------------------------------------------------------ MCP4725
class AnalogOut {
public:
  AnalogOut(uint8_t addr);
  bool begin();
  // Выставить напряжение на выходе ЦАП. Возвращает фактически достижимое
  // значение (шкала 12 бит, шаг ~0.8 мВ при VDD 3.3 В).
  float setMillivolts(float mv);
  float millivolts() const { return _mv; }
  bool present() const { return _present; }

private:
  uint8_t _addr;
  bool _present = false;
  float _mv = 0.0f;
};

// ------------------------------------------------- опорное напряжение
// Три отвода прецизионного делителя: 2.5 В / 250 мВ / 25 мВ.
class Reference {
public:
  explicit Reference(Expander &exp) : _exp(exp) {}
  void begin();
  // tap: 0 — выключено, 1..3 — отводы
  void select(uint8_t tap);
  uint8_t tap() const { return _tap; }
  float millivolts() const;

private:
  Expander &_exp;
  uint8_t _tap = 0;
};

// ------------------------------------------------------- генератор сигнала
class SignalGen {
public:
  void begin();
  // freqHz: 1..100000, dutyPercent: 0..100
  void set(uint32_t freqHz, float dutyPercent);
  void enable(bool on);
  uint32_t freqHz() const { return _freq; }
  float duty() const { return _duty; }
  bool enabled() const { return _on; }

  // Расчётные значения на прямом выходе (амплитуда = VDD, скважность D):
  float expectedDcMv() const;    // среднее   = A * D
  float expectedAcRmsMv() const; // AC-часть  = A * sqrt(D*(1-D))
  float expectedAcDcRmsMv() const; // полное   = A * sqrt(D)
  float crestFactor() const;     // 1/sqrt(D)

private:
  uint32_t _freq = LEDC_DEFAULT_HZ;
  float _duty = 50.0f;
  bool _on = false;
};

// ------------------------------------------------------- резисторная декада
struct DecadeEntry {
  const char *label;
  float ohms; // NAN = обрыв
};

class ResistorDecade {
public:
  void begin();
  void select(int8_t channel); // -1 — всё отключено
  int8_t channel() const { return _ch; }
  float ohms() const;
  const char *label() const;
  static const DecadeEntry *table();
  static uint8_t tableSize();

private:
  int8_t _ch = -1;
};

// ------------------------------------------------------- зоопарк компонентов
struct ComponentEntry {
  const char *label;
  const char *kind;    // diode | cap | short | open
  float value;         // В для диодов, Ф для конденсаторов
  bool faulty;         // заведомо неисправный экземпляр
};

class ComponentZoo {
public:
  void begin();
  void select(int8_t channel);
  int8_t channel() const { return _ch; }
  const ComponentEntry *entry() const;
  static const ComponentEntry *table();
  static uint8_t tableSize();

private:
  int8_t _ch = -1;
};

// ------------------------------------------------------------------ нагрузки
class Loads {
public:
  explicit Loads(Expander &exp) : _exp(exp) {}
  void begin();
  void set(bool led, bool mid, bool high);
  bool led() const { return _led; }
  bool mid() const { return _mid; }
  bool high() const { return _high; }
  float expectedCurrentMa() const;
  void allOff() { set(false, false, false); }

private:
  Expander &_exp;
  bool _led = false, _mid = false, _high = false;
};

// -------------------------------------------------------------- прочие узлы
class Ghost {
public:
  explicit Ghost(Expander &exp) : _exp(exp) {}
  void begin();
  void enable(bool on);
  bool enabled() const { return _on; }

private:
  Expander &_exp;
  bool _on = false;
};

class Inrush {
public:
  explicit Inrush(Expander &exp) : _exp(exp) {}
  void begin();
  void pulse(uint32_t onMs); // неблокирующе: реле само отпустит в tick()
  void tick();
  bool active() const { return _active; }

private:
  Expander &_exp;
  bool _active = false;
  uint32_t _offAt = 0;
};

class Chatter {
public:
  void begin();
  void configure(uint16_t periodMs, uint8_t breakMs);
  void enable(bool on);
  void tick();
  bool enabled() const { return _on; }

private:
  bool _on = false;
  uint16_t _period = 500;
  uint8_t _break = 10;
  uint32_t _next = 0;
  bool _open = false;
};

class Thermal {
public:
  explicit Thermal(Expander &exp) : _exp(exp) {}
  void begin();
  void setHeater(uint8_t percent);
  void setFan(bool on);
  uint8_t heater() const { return _heater; }
  bool fan() const { return _fan; }
  // Эталонная температура с DS18B20; NAN, если датчик не найден.
  float readReferenceC();

private:
  Expander &_exp;
  uint8_t _heater = 0;
  bool _fan = false;
  float _lastC = NAN;
  uint32_t _lastRead = 0;
};

// --------------------------------------------------------------- токовая петля
class CurrentLoop {
public:
  explicit CurrentLoop(AnalogOut &dac) : _dac(dac) {}
  void setPercent(float percent); // 0% = 4 мА, 100% = 20 мА
  void setMilliamps(float ma);
  float percent() const { return _percent; }
  float milliamps() const { return 4.0f + _percent * 0.16f; }

private:
  AnalogOut &_dac;
  float _percent = 0.0f;
};

// ------------------------------------------------------------------- фасад
struct Board {
  Expander expander;
  AnalogOut level{MCP4725_ADDR_LEVEL};
  AnalogOut loopDac{MCP4725_ADDR_LOOP};
  Reference reference{expander};
  SignalGen signal;
  ResistorDecade decade;
  ComponentZoo zoo;
  Loads loads{expander};
  Ghost ghost{expander};
  Inrush inrush{expander};
  Chatter chatter;
  Thermal thermal{expander};
  CurrentLoop loop{loopDac};

  void begin();
  void tick();
  void safeState(); // всё выключить: нагрузки, нагреватель, фантом, реле
};

extern Board board;

} // namespace hal
