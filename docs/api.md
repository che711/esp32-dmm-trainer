# HTTP API стенда

Базовый адрес — `http://192.168.4.1` (режим точки доступа) или адрес,
полученный в домашней сети. Все ответы — JSON в UTF-8, кроме `truth.csv`.

## GET /api/state

Полное состояние: прошивка, текущий сценарий и шаг, состояние всех модулей,
статус режима неисправностей.

```json
{
  "fw": "0.3.0",
  "uptime_s": 412,
  "run": {
    "state": "running",
    "id": "dcv_ladder",
    "step": 2,
    "steps": 7,
    "elapsed_ms": 65210,
    "quantity": "dcv",
    "truth": 1.0,
    "hint": "1 В с ЦАП",
    "truth_samples": 66
  },
  "hw": { "reference_tap": 0, "level_mv": 999.8, "signal": { "on": false } },
  "fault": { "active": false, "solved": false, "attempts": 0 }
}
```

## GET /api/scenarios

Список сценариев из LittleFS: `id`, `title`, `function`, число шагов.

## POST /api/scenario/start

Тело: `{"id": "dcv_ladder"}`. Сбрасывает ground-truth лог и запускает прогон.
Ответ — как у `/api/state`. Код 404, если сценарий не найден.

## POST /api/scenario/stop

Останавливает прогон и переводит железо в безопасное состояние.
Ground-truth лог сохраняется — его ещё можно скачать.

## GET /api/truth.csv

Эталонный лог текущего (или последнего) прогона:

```csv
t_ms,step,quantity,value
0,0,dcv,0.000000
1000,0,dcv,0.000000
5000,1,dcv,2.500000
```

`t_ms` — миллисекунды от старта сценария. Именно этот файл принимает
`dmm-trainer grade --truth`.

## POST /api/manual

Ручное управление; любой набор полей. Останавливает активный сценарий.

```json
{
  "reference_tap": 1,
  "level_mv": 1500,
  "decade": 3,
  "component": 0,
  "ghost": true,
  "chatter": false,
  "heater": 60,
  "fan": true,
  "loop_percent": 50,
  "inrush_ms": 400,
  "loads": {"led": true, "mid": false, "high": false},
  "signal": {"on": true, "hz": 1000, "duty": 25}
}
```

## POST /api/safe

Всё выключить: нагрузки, нагреватель, фантом, реле, сигнал.

## GET /api/catalog

Справочники: каналы резисторной декады и зоопарка компонентов с подписями.

## Режим неисправностей

- `GET /api/faults` — список возможных диагнозов (для выпадающего списка).
- `POST /api/fault/start` — `{"id": "..."}` или пустое тело для случайного случая.
- `POST /api/fault/answer` — `{"id": "..."}`; ответ `{"correct": true, "attempts": 2, "method": "..."}`.
- `POST /api/fault/stop` — сброс.

Идентификатор активного случая через API не отдаётся, пока он не разгадан —
иначе задание теряет смысл.
