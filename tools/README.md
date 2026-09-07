# tools/ — сверка логов

Пакет без внешних зависимостей (stdlib), Python 3.10+.

```bash
pip install -e .

dmm-trainer --host http://192.168.4.1 run dcv_ladder   # запустить сценарий
dmm-trainer --host http://192.168.4.1 fetch -o truth.csv
dmm-trainer grade --meter brymen_export.csv --truth truth.csv -v
```

Код возврата `grade`: 0 — все шаги в паспортном допуске, 1 — есть провалы,
2 — файлы не разобрались. Удобно вешать в CI.
