// SPDX-License-Identifier: MIT
// DMM Trainer — конфигурация железа.
//
// Целевая плата: Waveshare ESP32-C6-Zero (Arduino core 3.x / pioarduino).
// У C6 нет встроенного DAC, поэтому аналоговые уровни задаются MCP4725 по I2C.
// Все пины собраны здесь: если у вас другая разводка — правится только этот файл.

#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------- сеть
#define FW_VERSION "0.3.0"
#define AP_SSID "dmm-trainer"
#define AP_PASS "trainer123" // >= 8 символов, иначе AP поднимется открытым
#define HTTP_PORT 80

// Если заданы — сначала пробуем подключиться к домашней сети,
// при неудаче за WIFI_STA_TIMEOUT_MS поднимаем свою точку доступа.
#define WIFI_STA_SSID ""
#define WIFI_STA_PASS ""
#define WIFI_STA_TIMEOUT_MS 8000

// ---------------------------------------------------------------- I2C
#define PIN_I2C_SDA 6
#define PIN_I2C_SCL 7
#define MCP4725_ADDR_LEVEL 0x60 // канал «плавное DC»
#define MCP4725_ADDR_LOOP 0x61  // канал «петля 4-20 мА» (A0 подтянут к VDD)

// ------------------------------------------------- расширитель PCF8574
// Медленные линии (нагрузки, реле, отводы опоры) вынесены на расширитель:
// у ESP32-C6-Zero свободных GPIO меньше, чем модулей у стенда, а GPIO12/13
// заняты USB, GPIO16/17 — консолью UART0.
#define PCF8574_ADDR 0x20
#define EXP_LOAD_LED 0  // ~10 мА
#define EXP_LOAD_MID 1  // ~500 мА
#define EXP_LOAD_HIGH 2 // ~2,4 А
#define EXP_GHOST 3     // реле фантомного источника через 10 МОм
#define EXP_INRUSH 4    // реле пускового тока -> CREST
#define EXP_FAN 5       // вентилятор обдува пластины
#define EXP_REF_S0 6    // отводы прецизионного делителя (74HC4052)
#define EXP_REF_S1 7

// -------------------------------------------------------- CD4051 x2
// Адресные линии A/B/C общие для обоих мультиплексоров, разделены только
// входы разрешения INH: одновременно активен максимум один мультиплексор.
#define PIN_MUX_A 0
#define PIN_MUX_B 1
#define PIN_MUX_C 2
#define PIN_MUX_R_EN 3 // резисторная декада, активный низкий
#define PIN_MUX_C_EN 4 // зоопарк компонентов, активный низкий

// ---------------------------------------------------------------- сигналы
#define PIN_PWM_RAW 10      // прямой ШИМ: меандр для Logic Hz / Duty / крест-фактора
#define PIN_PWM_FILTERED 11 // тот же ШИМ через RC-фильтр 2-го порядка -> AC/AC+DC
#define LEDC_RES_BITS 10
#define LEDC_DEFAULT_HZ 1000

// -------------------------------------------------- прямые GPIO стенда
#define PIN_CHATTER 5     // дребезг контакта: нужны миллисекунды, поэтому не расширитель
#define PIN_HEATER 14     // ШИМ нагревателя -> T1/T2/T1-T2
#define PIN_ONEWIRE 18    // DS18B20 — независимый эталон температуры
#define PIN_STATUS_LED 19 // индикатор состояния стенда

// ------------------------------------------------------------ калибровка
// Реальные значения ваших резисторов после измерения эталонным прибором.
// Именно они попадают в ground-truth лог, а не номиналы с маркировки.
#define REF_VOLTAGE_MV 2500.0f  // ADR4525 / REF5025
#define DIV_TAP_1_RATIO 1.0f    // прямой выход опоры
#define DIV_TAP_2_RATIO 0.1f    // делитель 1:10
#define DIV_TAP_3_RATIO 0.01f   // делитель 1:100
#define VDD_MV 3300.0f          // питание MCP4725 = опора его шкалы

// Шунт токовой петли: ток = Vdac / R_SENSE
#define LOOP_R_SENSE_OHM 100.0f

#define TRUTH_BUFFER_SAMPLES 900 // 15 минут при 1 Гц
#define TRUTH_PERIOD_MS 1000
