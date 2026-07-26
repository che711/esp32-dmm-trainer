# SPDX-License-Identifier: MIT
"""Командная строка: скачать эталон со стенда и сверить с логом мультиметра."""

from __future__ import annotations

import argparse
import json
import math
import sys
import urllib.request
from pathlib import Path

from .grading import Report, grade
from .readers import ParseError, read_meter_csv, read_truth_csv

DEFAULT_HOST = "http://192.168.4.1"


def _fetch(url: str, timeout: float = 10.0) -> bytes:
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return resp.read()


def cmd_fetch(args: argparse.Namespace) -> int:
    url = f"{args.host.rstrip('/')}/api/truth.csv"
    data = _fetch(url)
    Path(args.out).write_bytes(data)
    lines = data.decode("utf-8", "replace").count("\n") - 1
    print(f"Сохранено {args.out}: {lines} точек эталона")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    host = args.host.rstrip("/")
    payload = json.dumps({"id": args.scenario}).encode()
    req = urllib.request.Request(
        f"{host}/api/scenario/start", data=payload,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        state = json.load(resp)
    run = state.get("run", {})
    print(f"Запущен сценарий {run.get('id')}: {run.get('title')}")
    print(f"Шагов: {run.get('steps')}. Следите за подсказками в web UI: {host}/")
    return 0


def _fmt(value: float | None, quantity: str) -> str:
    if value is None:
        return "—"
    if math.isnan(value):
        return "nan"
    unit = {
        "dcv": "В", "acv": "В", "acdcv": "В", "dcmv": "мВ", "ohm": "Ом",
        "ma": "мА", "loop_ma": "мА", "ua": "мкА", "percent": "%",
        "degc": "°C", "hz": "Гц", "duty": "%",
    }.get(quantity, "")
    if abs(value) >= 1e6:
        return f"{value / 1e6:.4f} М{unit}"
    if abs(value) >= 1e3 and quantity == "ohm":
        return f"{value / 1e3:.4f} к{unit}"
    return f"{value:.4f} {unit}".strip()


def print_report(report: Report, verbose: bool = False) -> None:
    print()
    print(f"Сдвиг логов: {report.offset:+.2f} с")
    print(f"{'Шаг':>3}  {'Величина':<9} {'Эталон':>14} {'Измерено':>14} "
          f"{'Ошибка':>10} {'Допуск':>12}  Итог")
    print("-" * 82)

    for r in report.results:
        step = r.step
        verdict = "—"
        if r.passed is True:
            verdict = "OK"
        elif r.passed is False:
            verdict = "ПРОВАЛ"

        err = ""
        if r.error is not None:
            pct = r.error_percent
            err = f"{r.error:+.4g}" + (f" ({pct:+.2f}%)" if pct is not None and verbose else "")

        tol = f"±{r.tolerance:.4g}" if r.tolerance is not None else "—"
        print(f"{step.index:>3}  {step.quantity:<9} {_fmt(step.value, step.quantity):>14} "
              f"{_fmt(r.measured, step.quantity):>14} {err:>10} {tol:>12}  {verdict}")
        if r.note:
            print(f"     └─ {r.note}")

    print("-" * 82)
    print(f"Пройдено: {report.passed}   Провалено: {report.failed}   "
          f"Пропущено: {report.skipped}")
    if report.ok:
        print("Все измерения укладываются в паспортную точность BM788BT.")
    elif report.failed:
        print("Есть выходы за паспортный допуск — смотрите шаги с пометкой ПРОВАЛ.")


def cmd_grade(args: argparse.Namespace) -> int:
    try:
        series = read_meter_csv(
            args.meter, time_col=args.time_col,
            value_col=args.value_col, unit_col=args.unit_col,
        )
        points = read_truth_csv(args.truth)
    except (ParseError, FileNotFoundError) as exc:
        print(f"Ошибка: {exc}", file=sys.stderr)
        return 2

    print(f"Лог мультиметра: {len(series)} точек, {series.duration:.1f} с")
    print(f"Эталон стенда:   {len(points)} точек")

    report = grade(series, points, offset=args.offset, freq_hint=args.freq)
    print_report(report, verbose=args.verbose)

    if args.json:
        Path(args.json).write_text(
            json.dumps(
                {
                    "offset": report.offset,
                    "passed": report.passed,
                    "failed": report.failed,
                    "skipped": report.skipped,
                    "steps": [
                        {
                            "index": r.step.index,
                            "quantity": r.step.quantity,
                            "truth": r.step.value,
                            "measured": r.measured,
                            "tolerance": r.tolerance,
                            "passed": r.passed,
                            "note": r.note,
                        }
                        for r in report.results
                    ],
                },
                ensure_ascii=False,
                indent=2,
            ),
            encoding="utf-8",
        )
        print(f"Отчёт сохранён: {args.json}")

    return 0 if report.ok else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="dmm-trainer",
        description="Сверка логов Brymen BM788BT с эталоном стенда DMM Trainer",
    )
    parser.add_argument("--host", default=DEFAULT_HOST, help="адрес стенда")
    sub = parser.add_subparsers(dest="command", required=True)

    p_run = sub.add_parser("run", help="запустить сценарий на стенде")
    p_run.add_argument("scenario", help="id сценария, например dcv_ladder")
    p_run.set_defaults(func=cmd_run)

    p_fetch = sub.add_parser("fetch", help="скачать ground-truth лог со стенда")
    p_fetch.add_argument("-o", "--out", default="truth.csv")
    p_fetch.set_defaults(func=cmd_fetch)

    p_grade = sub.add_parser("grade", help="сверить лог мультиметра с эталоном")
    p_grade.add_argument("--meter", required=True, help="CSV из приложения Brymen")
    p_grade.add_argument("--truth", required=True, help="truth.csv со стенда")
    p_grade.add_argument("--offset", type=float, default=None,
                         help="сдвиг логов в секундах (по умолчанию подбирается)")
    p_grade.add_argument("--freq", type=float, default=None,
                         help="частота сигнала, Гц — уточняет допуск для AC")
    p_grade.add_argument("--time-col", type=int, default=None)
    p_grade.add_argument("--value-col", type=int, default=None)
    p_grade.add_argument("--unit-col", type=int, default=None)
    p_grade.add_argument("--json", default=None, help="сохранить отчёт в JSON")
    p_grade.add_argument("-v", "--verbose", action="store_true")
    p_grade.set_defaults(func=cmd_grade)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
