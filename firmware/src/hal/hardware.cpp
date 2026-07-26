// SPDX-License-Identifier: MIT
#include "../config.h"
#include "hardware.h"
#include <Wire.h>
#include <math.h>

#if __has_include(<OneWire.h>) && __has_include(<DallasTemperature.h>)
#define HAS_DS18B20 1
#include <DallasTemperature.h>
#include <OneWire.h>
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature dsSensors(&oneWire);
#endif

namespace hal {

Board board;

// ------------------------------------------------------------ PCF8574
bool Expander::begin() {
  Wire.beginTransmission(PCF8574_ADDR);
  _present = (Wire.endTransmission() == 0);
  _shadow = 0x00;
  if (_present) flush();
  return _present;
}

void Expander::flush() {
  if (!_present) return;
  Wire.beginTransmission(PCF8574_ADDR);
  Wire.write(_shadow);
  Wire.endTransmission();
}

void Expander::write(uint8_t bit, bool value) {
  if (bit > 7) return;
  const uint8_t mask = 1 << bit;
  const uint8_t next = value ? (_shadow | mask) : (uint8_t)(_shadow & ~mask);
  if (next == _shadow) return;
  _shadow = next;
  flush();
}

void Expander::writeAll(uint8_t mask) {
  if (mask == _shadow) return;
  _shadow = mask;
  flush();
}

// ------------------------------------------------------------------ MCP4725
AnalogOut::AnalogOut(uint8_t addr) : _addr(addr) {}

bool AnalogOut::begin() {
  Wire.beginTransmission(_addr);
  _present = (Wire.endTransmission() == 0);
  if (_present) setMillivolts(0);
  return _present;
}

float AnalogOut::setMillivolts(float mv) {
  if (!_present) return NAN;
  if (mv < 0) mv = 0;
  if (mv > VDD_MV) mv = VDD_MV;
  uint16_t code = (uint16_t)lroundf(mv / VDD_MV * 4095.0f);
  Wire.beginTransmission(_addr);
  Wire.write(0x40); // запись в регистр ЦАП без EEPROM
  Wire.write(code >> 4);
  Wire.write((code & 0x0F) << 4);
  Wire.endTransmission();
  _mv = code / 4095.0f * VDD_MV; // фактически достижимое значение
  return _mv;
}

// -------------------------------------------------------------- опора
void Reference::begin() { select(0); }

void Reference::select(uint8_t tap) {
  _tap = tap > 3 ? 0 : tap;
  _exp.write(EXP_REF_S0, _tap & 0x01);
  _exp.write(EXP_REF_S1, (_tap >> 1) & 0x01);
}

float Reference::millivolts() const {
  switch (_tap) {
  case 1: return REF_VOLTAGE_MV * DIV_TAP_1_RATIO;
  case 2: return REF_VOLTAGE_MV * DIV_TAP_2_RATIO;
  case 3: return REF_VOLTAGE_MV * DIV_TAP_3_RATIO;
  default: return 0.0f;
  }
}

// -------------------------------------------------------- генератор сигнала
void SignalGen::begin() {
  ledcAttach(PIN_PWM_RAW, LEDC_DEFAULT_HZ, LEDC_RES_BITS);
  ledcAttach(PIN_PWM_FILTERED, LEDC_DEFAULT_HZ, LEDC_RES_BITS);
  enable(false);
}

void SignalGen::set(uint32_t freqHz, float dutyPercent) {
  if (freqHz < 1) freqHz = 1;
  if (freqHz > 100000UL) freqHz = 100000UL;
  if (dutyPercent < 0) dutyPercent = 0;
  if (dutyPercent > 100) dutyPercent = 100;
  _freq = freqHz;
  _duty = dutyPercent;
  ledcChangeFrequency(PIN_PWM_RAW, _freq, LEDC_RES_BITS);
  ledcChangeFrequency(PIN_PWM_FILTERED, _freq, LEDC_RES_BITS);
  if (_on) enable(true);
}

void SignalGen::enable(bool on) {
  _on = on;
  const uint32_t maxDuty = (1UL << LEDC_RES_BITS) - 1;
  uint32_t value = on ? (uint32_t)lroundf(_duty / 100.0f * maxDuty) : 0;
  ledcWrite(PIN_PWM_RAW, value);
  ledcWrite(PIN_PWM_FILTERED, value);
}

float SignalGen::expectedDcMv() const {
  return _on ? VDD_MV * (_duty / 100.0f) : 0.0f;
}

float SignalGen::expectedAcRmsMv() const {
  if (!_on) return 0.0f;
  const float d = _duty / 100.0f;
  return VDD_MV * sqrtf(d * (1.0f - d)); // RMS переменной составляющей меандра
}

float SignalGen::expectedAcDcRmsMv() const {
  if (!_on) return 0.0f;
  return VDD_MV * sqrtf(_duty / 100.0f);
}

float SignalGen::crestFactor() const {
  const float d = _duty / 100.0f;
  return d > 0 ? 1.0f / sqrtf(d) : INFINITY;
}

// ------------------------------------------------------- резисторная декада
static const DecadeEntry kDecade[] = {
    {"1 Ohm", 1.0f},        {"100 Ohm", 100.0f},
    {"1 kOhm", 1000.0f},    {"10 kOhm", 10000.0f},
    {"1 MOhm", 1000000.0f}, {"47 MOhm", 47000000.0f},
    {"1 GOhm (1 nS)", 1000000000.0f}, {"open", NAN},
};

const DecadeEntry *ResistorDecade::table() { return kDecade; }
uint8_t ResistorDecade::tableSize() { return sizeof(kDecade) / sizeof(kDecade[0]); }

// Адресные линии общие для обоих мультиплексоров, поэтому при выборе канала
// на одном второй обязательно запрещается — иначе на общую шину сядут сразу
// два канала и измерение окажется мусором.
static void muxWrite(uint8_t en, uint8_t otherEn, int8_t ch) {
  digitalWrite(otherEn, HIGH);
  if (ch < 0) {
    digitalWrite(en, HIGH); // INH — активный низкий, HIGH = всё отключено
    return;
  }
  digitalWrite(PIN_MUX_A, ch & 0x01);
  digitalWrite(PIN_MUX_B, (ch >> 1) & 0x01);
  digitalWrite(PIN_MUX_C, (ch >> 2) & 0x01);
  digitalWrite(en, LOW);
}

void ResistorDecade::begin() {
  pinMode(PIN_MUX_A, OUTPUT);
  pinMode(PIN_MUX_B, OUTPUT);
  pinMode(PIN_MUX_C, OUTPUT);
  pinMode(PIN_MUX_R_EN, OUTPUT);
  digitalWrite(PIN_MUX_R_EN, HIGH);
  select(-1);
}

void ResistorDecade::select(int8_t channel) {
  if (channel >= (int8_t)tableSize()) channel = -1;
  _ch = channel;
  muxWrite(PIN_MUX_R_EN, PIN_MUX_C_EN, _ch);
}

float ResistorDecade::ohms() const { return _ch < 0 ? NAN : kDecade[_ch].ohms; }
const char *ResistorDecade::label() const { return _ch < 0 ? "off" : kDecade[_ch].label; }

// ------------------------------------------------------- зоопарк компонентов
static const ComponentEntry kZoo[] = {
    {"1N4148 (Si)", "diode", 0.65f, false},
    {"SS14 (Schottky)", "diode", 0.30f, false},
    {"LED red", "diode", 1.85f, false},
    {"1N4148 shorted", "diode", 0.0f, true},
    {"1N4148 open", "diode", NAN, true},
    {"1 uF film", "cap", 1e-6f, false},
    {"470 uF electrolytic", "cap", 470e-6f, false},
    {"1000 uF, degraded", "cap", 220e-6f, true},
};

const ComponentEntry *ComponentZoo::table() { return kZoo; }
uint8_t ComponentZoo::tableSize() { return sizeof(kZoo) / sizeof(kZoo[0]); }

void ComponentZoo::begin() {
  pinMode(PIN_MUX_C_EN, OUTPUT);
  digitalWrite(PIN_MUX_C_EN, HIGH);
  select(-1);
}

void ComponentZoo::select(int8_t channel) {
  if (channel >= (int8_t)tableSize()) channel = -1;
  _ch = channel;
  muxWrite(PIN_MUX_C_EN, PIN_MUX_R_EN, _ch);
}

const ComponentEntry *ComponentZoo::entry() const { return _ch < 0 ? nullptr : &kZoo[_ch]; }

// ------------------------------------------------------------------ нагрузки
void Loads::begin() { allOff(); }

void Loads::set(bool led, bool mid, bool high) {
  _led = led; _mid = mid; _high = high;
  _exp.write(EXP_LOAD_LED, led);
  _exp.write(EXP_LOAD_MID, mid);
  _exp.write(EXP_LOAD_HIGH, high);
}

float Loads::expectedCurrentMa() const {
  // Ориентировочные значения; уточняются калибровкой под ваш блок питания.
  return (_led ? 10.0f : 0.0f) + (_mid ? 500.0f : 0.0f) + (_high ? 2400.0f : 0.0f);
}

// ------------------------------------------------------------------- фантом
void Ghost::begin() { enable(false); }
void Ghost::enable(bool on) { _on = on; _exp.write(EXP_GHOST, on); }

// ------------------------------------------------------------------- инраш
void Inrush::begin() { _exp.write(EXP_INRUSH, false); }

void Inrush::pulse(uint32_t onMs) {
  _exp.write(EXP_INRUSH, true);
  _active = true;
  _offAt = millis() + onMs;
}

void Inrush::tick() {
  if (_active && (int32_t)(millis() - _offAt) >= 0) {
    _exp.write(EXP_INRUSH, false);
    _active = false;
  }
}

// ------------------------------------------------------------------ дребезг
void Chatter::begin() { pinMode(PIN_CHATTER, OUTPUT); digitalWrite(PIN_CHATTER, HIGH); }

void Chatter::configure(uint16_t periodMs, uint8_t breakMs) {
  _period = periodMs < 20 ? 20 : periodMs;
  _break = breakMs < 1 ? 1 : breakMs;
}

void Chatter::enable(bool on) {
  _on = on;
  _open = false;
  digitalWrite(PIN_CHATTER, HIGH); // норма — контакт замкнут
  _next = millis() + _period;
}

void Chatter::tick() {
  if (!_on) return;
  const uint32_t now = millis();
  if ((int32_t)(now - _next) < 0) return;
  if (_open) {
    digitalWrite(PIN_CHATTER, HIGH);
    _open = false;
    _next = now + _period;
  } else {
    digitalWrite(PIN_CHATTER, LOW); // микроразрыв
    _open = true;
    _next = now + _break;
  }
}

// -------------------------------------------------------------- термоузел
void Thermal::begin() {
  ledcAttach(PIN_HEATER, 200, 8);
  ledcWrite(PIN_HEATER, 0);
  _exp.write(EXP_FAN, false);
#ifdef HAS_DS18B20
  dsSensors.begin();
  dsSensors.setWaitForConversion(false);
  dsSensors.requestTemperatures();
#endif
}

void Thermal::setHeater(uint8_t percent) {
  _heater = percent > 100 ? 100 : percent;
  ledcWrite(PIN_HEATER, (uint32_t)lroundf(_heater * 2.55f));
}

void Thermal::setFan(bool on) { _fan = on; _exp.write(EXP_FAN, on); }

float Thermal::readReferenceC() {
#ifdef HAS_DS18B20
  const uint32_t now = millis();
  if (now - _lastRead > 1000) {
    _lastRead = now;
    const float c = dsSensors.getTempCByIndex(0);
    dsSensors.requestTemperatures();
    _lastC = (c < -100.0f) ? NAN : c;
  }
#endif
  return _lastC;
}

// ------------------------------------------------------------- токовая петля
void CurrentLoop::setPercent(float percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  _percent = percent;
  const float ma = milliamps();
  _dac.setMillivolts(ma * LOOP_R_SENSE_OHM); // I = Vdac / Rsense
}

void CurrentLoop::setMilliamps(float ma) {
  setPercent((ma - 4.0f) / 0.16f);
}

// ------------------------------------------------------------------- фасад
void Board::begin() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  if (!expander.begin()) {
    Serial.println("[hal] PCF8574 не найден: нагрузки и реле недоступны");
  }
  level.begin();
  loopDac.begin();
  reference.begin();
  signal.begin();
  decade.begin();
  zoo.begin();
  loads.begin();
  ghost.begin();
  inrush.begin();
  chatter.begin();
  thermal.begin();
  safeState();
}

void Board::tick() {
  inrush.tick();
  chatter.tick();
}

void Board::safeState() {
  loads.allOff();
  ghost.enable(false);
  chatter.enable(false);
  thermal.setHeater(0);
  thermal.setFan(false);
  signal.enable(false);
  decade.select(-1);
  zoo.select(-1);
  reference.select(0);
  level.setMillivolts(0);
  loop.setPercent(0);
}

} // namespace hal
