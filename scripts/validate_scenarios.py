#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Проверка сценариев: структура, известные величины, разумные длительности.

Запускается в CI — сломанный сценарий не должен доехать до флеша, где он
молча не загрузится и стенд будет вести себя непонятно.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

KNOWN_QUANTITIES = {
    "dcv", "dcmv", "acv", "acdcv", "ohm", "ns", "ma", "ua",
    "loop_ma", "percent", "degc", "hz", "duty", "farad", "",
}

KNOWN_STEP_KEYS = {
    "ms", "quantity", "truth", "hint", "reference_tap", "level_mv", "decade",
    "component", "ghost", "chatter", "heater", "fan", "loop_percent",
    "inrush_ms", "loads", "signal", "signal_off",
}

MAX_STEP_MS = 10 * 60 * 1000


def check(path: Path) -> list[str]:
    problems: list[str] = []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{path.name}: не разбирается как JSON — {exc}"]

    for field in ("id", "title", "steps"):
        if field not in data:
            problems.append(f"{path.name}: нет обязательного поля {field!r}")

    if data.get("id") and path.stem != data["id"]:
        problems.append(f"{path.name}: id {data['id']!r} не совпадает с именем файла")

    steps = data.get("steps") or []
    if not steps:
        problems.append(f"{path.name}: пустой список шагов")

    total_ms = 0
    for i, step in enumerate(steps):
        prefix = f"{path.name}, шаг {i}"
        unknown = set(step) - KNOWN_STEP_KEYS
        if unknown:
            problems.append(f"{prefix}: неизвестные ключи {sorted(unknown)}")

        quantity = step.get("quantity", "")
        if quantity not in KNOWN_QUANTITIES:
            problems.append(f"{prefix}: неизвестная величина {quantity!r}")

        ms = step.get("ms", 10000)
        if not isinstance(ms, int) or ms <= 0:
            problems.append(f"{prefix}: некорректная длительность {ms!r}")
        elif ms > MAX_STEP_MS:
            problems.append(f"{prefix}: шаг длиннее 10 минут")
        else:
            total_ms += ms

        duty = (step.get("signal") or {}).get("duty")
        if duty is not None and not 0 <= duty <= 100:
            problems.append(f"{prefix}: скважность вне 0..100")

        heater = step.get("heater")
        if heater is not None and not 0 <= heater <= 100:
            problems.append(f"{prefix}: нагрев вне 0..100%")

    # Буфер эталона на стенде — TRUTH_BUFFER_SAMPLES точек при 1 Гц
    if total_ms / 1000 > 900:
        problems.append(
            f"{path.name}: сценарий длиннее 15 минут ({total_ms // 1000} с) — "
            "не поместится в буфер ground truth"
        )
    return problems


def main() -> int:
    root = Path(__file__).resolve().parent.parent / "scenarios"
    files = sorted(root.glob("*.json"))
    if not files:
        print("сценарии не найдены", file=sys.stderr)
        return 1

    all_problems: list[str] = []
    for path in files:
        problems = check(path)
        status = "OK" if not problems else "ОШИБКИ"
        print(f"{path.name:<24} {status}")
        all_problems.extend(problems)

    if all_problems:
        print()
        for p in all_problems:
            print(f"  - {p}", file=sys.stderr)
        return 1

    print(f"\nПроверено сценариев: {len(files)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
