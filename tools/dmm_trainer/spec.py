# SPDX-License-Identifier: MIT
"""Паспортная точность Brymen BM788BT в виде кода.

Данные взяты из раздела ELECTRICAL SPECIFICATIONS руководства пользователя
BM788BT/BM789/BM785. Погрешность записывается как ±(% от показания + N единиц
младшего разряда), поэтому нужно знать не только функцию, но и предел: от него
зависит «вес» одной единицы.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

__all__ = ["Range", "tolerance", "pick_range", "RANGES", "SUPPORTED_FUNCTIONS"]


@dataclass(frozen=True)
class Range:
    """Один предел измерения."""

    full_scale: float  # верхняя граница в базовых единицах (В, Ом, А, °C…)
    digit: float  # вес единицы младшего разряда
    percent: float  # процент от показания
    digits: float  # число единиц младшего разряда
    fmin: Optional[float] = None  # нижняя граница частоты, Гц
    fmax: Optional[float] = None  # верхняя граница частоты, Гц

    def contains_freq(self, freq: Optional[float]) -> bool:
        if self.fmin is None and self.fmax is None:
            return True
        if freq is None:
            return False
        lo = self.fmin if self.fmin is not None else 0.0
        hi = self.fmax if self.fmax is not None else float("inf")
        return lo <= freq <= hi

    def tolerance(self, reading: float) -> float:
        return abs(reading) * self.percent / 100.0 + self.digits * self.digit


# Пределы перечисляются по возрастанию full_scale. Для функций с частотной
# зависимостью один и тот же предел встречается несколько раз с разными
# диапазонами частот — выбирается подходящий по freq.
RANGES: dict[str, list[Range]] = {
    # ------------------------------------------------------------- напряжение
    "dcv": [
        Range(0.6, 1e-5, 0.03, 2),
        Range(6.0, 1e-4, 0.03, 2),
        Range(60.0, 1e-3, 0.03, 2),
        Range(600.0, 1e-2, 0.05, 5),
        Range(1000.0, 1e-1, 0.15, 5),
    ],
    "acv": [
        # 50–60 Гц
        Range(0.6, 1e-5, 0.5, 30, 45, 65),
        Range(6.0, 1e-4, 0.5, 30, 45, 65),
        Range(60.0, 1e-3, 0.5, 30, 45, 65),
        Range(600.0, 1e-2, 0.5, 30, 45, 65),
        Range(1000.0, 1e-1, 0.5, 30, 45, 65),
        # 40 Гц – 1 кГц
        Range(0.6, 1e-5, 0.9, 30, 40, 1000),
        Range(6.0, 1e-4, 0.9, 30, 40, 1000),
        Range(60.0, 1e-3, 0.9, 30, 40, 1000),
        Range(600.0, 1e-2, 0.9, 30, 40, 1000),
        Range(1000.0, 1e-1, 0.9, 30, 40, 1000),
        # 1–7 кГц
        Range(0.6, 1e-5, 1.8, 40, 1000, 7000),
        Range(6.0, 1e-4, 1.8, 40, 1000, 7000),
        Range(60.0, 1e-3, 1.8, 40, 1000, 7000),
        Range(600.0, 1e-2, 1.8, 40, 1000, 7000),
        # 7–20 кГц
        Range(0.6, 1e-5, 2.0, 60, 7000, 20000),
        Range(6.0, 1e-4, 2.0, 60, 7000, 20000),
        Range(60.0, 1e-3, 2.0, 60, 7000, 20000),
        # 20–100 кГц
        Range(0.6, 1e-5, 4.0, 60, 20000, 100000),
        Range(6.0, 1e-4, 4.0, 60, 20000, 100000),
        Range(60.0, 1e-3, 4.0, 60, 20000, 100000),
    ],
    "acdcv": [
        Range(0.6, 1e-5, 0.7, 40, 45, 65),
        Range(6.0, 1e-4, 0.7, 40, 45, 65),
        Range(60.0, 1e-3, 0.7, 40, 45, 65),
        Range(600.0, 1e-2, 0.7, 40, 45, 65),
        Range(1000.0, 1e-1, 0.7, 40, 45, 65),
        Range(0.6, 1e-5, 1.2, 40, 0, 1000),
        Range(6.0, 1e-4, 1.2, 40, 0, 1000),
        Range(60.0, 1e-3, 1.2, 40, 0, 1000),
        Range(600.0, 1e-2, 1.2, 40, 0, 1000),
        Range(1000.0, 1e-1, 1.2, 40, 0, 1000),
        Range(0.6, 1e-5, 2.0, 50, 1000, 7000),
        Range(6.0, 1e-4, 2.0, 50, 1000, 7000),
        Range(60.0, 1e-3, 2.0, 50, 1000, 7000),
        Range(600.0, 1e-2, 2.0, 50, 1000, 7000),
        Range(0.6, 1e-5, 2.5, 70, 7000, 20000),
        Range(6.0, 1e-4, 2.5, 70, 7000, 20000),
        Range(60.0, 1e-3, 2.5, 70, 7000, 20000),
    ],
    # ---------------------------------------------------------- сопротивление
    "ohm": [
        Range(600.0, 1e-2, 0.085, 10),
        Range(6e3, 1e-1, 0.085, 4),
        Range(60e3, 1.0, 0.085, 4),
        Range(600e3, 10.0, 0.15, 4),
        Range(6e6, 100.0, 1.5, 5),
        Range(60e6, 1e3, 2.0, 5),
    ],
    "ns": [Range(99.99, 1e-2, 1.0, 10)],
    # ------------------------------------------------------------------- ток
    "dca": [
        Range(600e-6, 1e-8, 0.075, 20),
        Range(6000e-6, 1e-7, 0.075, 20),
        Range(60e-3, 1e-6, 0.075, 20),
        Range(600e-3, 1e-5, 0.15, 20),
        Range(6.0, 1e-4, 0.3, 20),
        Range(10.0, 1e-3, 0.3, 30),
    ],
    "aca": [
        Range(600e-6, 1e-8, 0.9, 20, 40, 3000),
        Range(6000e-6, 1e-7, 0.9, 20, 40, 3000),
        Range(60e-3, 1e-6, 0.9, 20, 40, 3000),
        Range(600e-3, 1e-5, 0.9, 20, 40, 3000),
        Range(6.0, 1e-4, 1.0, 30, 40, 3000),
        Range(10.0, 1e-3, 1.0, 30, 40, 3000),
    ],
    # ------------------------------------------------------------ прочие
    "loop_percent": [Range(100.0, 1e-2, 0.0, 25)],
    "degc": [Range(1090.0, 0.1, 1.0, 10)],  # 1,0% + 1,0 °C = 10 единиц по 0,1 °C
    "logic_hz": [Range(1e6, 1e-3, 0.002, 4)],
    "line_hz": [Range(50e3, 1e-3, 0.05, 5)],
    "capacitance": [
        Range(10e-9, 1e-12, 1.0, 10),
        Range(1000e-9, 1e-10, 1.0, 2),
        Range(1e-3, 1e-7, 1.8, 4),
        Range(10e-3, 1e-6, 2.0, 4),
    ],
}

SUPPORTED_FUNCTIONS = tuple(RANGES.keys())

# Как величины из ground-truth лога отображаются на функции прибора.
QUANTITY_TO_FUNCTION = {
    "dcv": "dcv",
    "dcmv": "dcv",
    "acv": "acv",
    "acdcv": "acdcv",
    "ohm": "ohm",
    "ns": "ns",
    "ma": "dca",
    "ua": "dca",
    "loop_ma": "dca",
    "percent": "loop_percent",
    "degc": "degc",
    "hz": "logic_hz",
    "duty": None,  # скважность считается отдельно, см. duty_tolerance()
    "farad": "capacitance",
}


class UnknownFunction(ValueError):
    pass


def pick_range(function: str, reading: float, freq: Optional[float] = None) -> Range:
    """Наименьший предел, вмещающий показание (как это делает автовыбор)."""
    if function not in RANGES:
        raise UnknownFunction(f"нет паспортных данных для функции {function!r}")

    candidates = [r for r in RANGES[function] if r.contains_freq(freq)]
    if not candidates:
        # частота вне нормируемой полосы — берём худший (самый широкий) вариант
        candidates = list(RANGES[function])

    fitting = [r for r in candidates if abs(reading) <= r.full_scale]
    if fitting:
        return min(fitting, key=lambda r: r.full_scale)
    return max(candidates, key=lambda r: r.full_scale)


def duty_tolerance(reading_percent: float, freq_hz: float) -> float:
    """Скважность: паспорт даёт 3 единицы на килогерц + 2 единицы."""
    digit = 0.01
    return (3.0 * max(freq_hz, 0.0) / 1000.0 + 2.0) * digit


def tolerance(
    quantity: str,
    reading: float,
    freq: Optional[float] = None,
) -> float:
    """Абсолютный допуск для величины из ground-truth лога.

    >>> round(tolerance("dcv", 2.5), 6)
    0.00095
    """
    if quantity == "duty":
        return duty_tolerance(reading, freq or 0.0)

    function = QUANTITY_TO_FUNCTION.get(quantity, quantity)
    if function is None or function not in RANGES:
        raise UnknownFunction(f"неизвестная величина {quantity!r}")

    value = reading
    if quantity == "dcmv":  # лог в милливольтах, паспорт — в вольтах
        value = reading / 1000.0
    elif quantity in ("ma", "loop_ma"):
        value = reading / 1000.0
    elif quantity == "ua":
        value = reading / 1e6

    rng = pick_range(function, value, freq)
    tol = rng.tolerance(value)

    if quantity == "dcmv":
        return tol * 1000.0
    if quantity in ("ma", "loop_ma"):
        return tol * 1000.0
    if quantity == "ua":
        return tol * 1e6
    return tol
