# SPDX-License-Identifier: MIT
"""Синхронизация двух рядов и выставление оценки.

Стенд и мультиметр стартуют независимо, поэтому лог мультиметра сдвинут во
времени на неизвестную величину. Сдвиг ищется перебором: берётся тот, при
котором медианная относительная ошибка по всем шагам минимальна. Это
устойчивее корреляции, когда шагов мало, а значения различаются на порядки.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from statistics import median
from typing import Optional, Sequence

from .readers import Sample, Series, TruthPoint
from .spec import UnknownFunction, tolerance

__all__ = ["Step", "StepResult", "Report", "steps_from_truth", "find_offset", "grade"]

SETTLE_FRACTION = 0.35  # первые 35% шага отбрасываем: щупы и прибор устаканиваются
MIN_SAMPLES = 2
MIN_COVERAGE = 0.25  # доля точек лога, обязанная попасть в интервал эталона


@dataclass
class Step:
    index: int
    quantity: str
    value: float
    t_start: float
    t_end: float

    @property
    def duration(self) -> float:
        return self.t_end - self.t_start


@dataclass
class StepResult:
    step: Step
    measured: Optional[float]
    n_samples: int
    tolerance: Optional[float] = None
    note: str = ""

    @property
    def error(self) -> Optional[float]:
        if self.measured is None or math.isnan(self.step.value):
            return None
        return self.measured - self.step.value

    @property
    def error_percent(self) -> Optional[float]:
        err = self.error
        if err is None or self.step.value == 0:
            return None
        return err / abs(self.step.value) * 100.0

    @property
    def passed(self) -> Optional[bool]:
        if self.measured is None or self.tolerance is None:
            return None
        err = self.error
        return err is not None and abs(err) <= self.tolerance


@dataclass
class Report:
    results: list[StepResult]
    offset: float
    scenario: str = ""

    @property
    def graded(self) -> list[StepResult]:
        return [r for r in self.results if r.passed is not None]

    @property
    def passed(self) -> int:
        return sum(1 for r in self.graded if r.passed)

    @property
    def failed(self) -> int:
        return sum(1 for r in self.graded if not r.passed)

    @property
    def skipped(self) -> int:
        return len(self.results) - len(self.graded)

    @property
    def ok(self) -> bool:
        return self.failed == 0 and self.passed > 0


def steps_from_truth(points: Sequence[TruthPoint]) -> list[Step]:
    """Схлопывает поточечный ground-truth лог в интервалы-шаги."""
    steps: list[Step] = []
    if not points:
        return steps

    current_index = points[0].step
    start = points[0].t
    quantity = points[0].quantity
    values = [points[0].value]

    for p in points[1:]:
        if p.step != current_index:
            steps.append(
                Step(
                    index=current_index,
                    quantity=quantity,
                    value=_representative(values),
                    t_start=start,
                    t_end=p.t,
                )
            )
            current_index, start, quantity, values = p.step, p.t, p.quantity, [p.value]
        else:
            values.append(p.value)

    last = points[-1]
    steps.append(
        Step(
            index=current_index,
            quantity=quantity,
            value=_representative(values),
            t_start=start,
            t_end=last.t + 1.0,
        )
    )
    return steps


def _representative(values: Sequence[float]) -> float:
    clean = [v for v in values if not math.isnan(v)]
    if not clean:
        return float("nan")
    return median(clean)


def _measure(series: Series, step: Step, offset: float) -> tuple[Optional[float], int]:
    settle = step.duration * SETTLE_FRACTION
    lo = step.t_start + settle + offset
    hi = step.t_end + offset
    window = [s.value for s in series.window(lo, hi) if not math.isnan(s.value)]
    if len(window) < MIN_SAMPLES:
        return (None, len(window))
    return (median(window), len(window))


def _truth_at(steps: Sequence[Step], t: float) -> Optional[float]:
    """Значение эталона в момент t (ступенчатая функция).

    Интервалы полуоткрытые [t_start, t_end): граница принадлежит следующему
    шагу. Иначе в точке переключения эталон и лог гарантированно расходятся,
    и поиск сдвига начинает предпочитать смещения «мимо» границ.
    """
    lo, hi = 0, len(steps) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        step = steps[mid]
        if t < step.t_start:
            hi = mid - 1
        elif t >= step.t_end and mid != len(steps) - 1:
            lo = mid + 1
        elif t > step.t_end:
            lo = mid + 1
        else:
            return step.value
    return None


def find_offset(
    series: Series,
    steps: Sequence[Step],
    search: float = 120.0,
    coarse: float = 1.0,
    max_samples: int = 2000,
) -> float:
    """Ищет сдвиг лога мультиметра относительно стенда, в секундах.

    Ошибка считается поточечно по всему ряду, а не по шагам: именно моменты
    переключения несут информацию о сдвиге. Усреднение здесь намеренно
    арифметическое — медиана слишком устойчива и «не замечает» рассинхрон,
    пока совпадает больше половины точек.
    """
    if not len(series) or not steps:
        return 0.0

    samples = [s for s in series.samples if not math.isnan(s.value)]
    if not samples:
        return 0.0
    if len(samples) > max_samples:  # прореживаем длинные логи
        stride = len(samples) // max_samples + 1
        samples = samples[::stride]

    def cost(offset: float) -> float:
        errors = []
        for s in samples:
            truth = _truth_at(steps, s.t - offset)
            if truth is None or math.isnan(truth):
                continue
            scale = max(abs(truth), 1e-9)
            errors.append(min(abs(s.value - truth) / scale, 1.0))
        if not errors:
            return float("inf")
        coverage = len(errors) / len(samples)
        if coverage < MIN_COVERAGE:
            # экстремальные сдвиги, где в диапазон эталона попала пара
            # случайно удачных точек, не должны выигрывать
            return float("inf")
        # плавный штраф здесь дал бы систематическое смещение оптимума
        # в сторону нуля, поэтому перекрытие — жёсткий отсекающий критерий
        return sum(errors) / len(errors)

    best_offset, best_cost = 0.0, cost(0.0)

    span = int(search / coarse)
    for i in range(-span, span + 1):
        candidate = i * coarse
        c = cost(candidate)
        if c < best_cost:
            best_offset, best_cost = candidate, c

    fine = best_offset
    for i in range(-10, 11):
        candidate = best_offset + i * coarse / 10.0
        c = cost(candidate)
        if c < best_cost:
            fine, best_cost = candidate, c

    return round(fine, 2)


def grade(
    series: Series,
    points: Sequence[TruthPoint],
    offset: Optional[float] = None,
    scenario: str = "",
    freq_hint: Optional[float] = None,
) -> Report:
    """Сверяет лог мультиметра с эталоном и возвращает отчёт."""
    steps = steps_from_truth(points)
    if offset is None:
        offset = find_offset(series, steps)

    results: list[StepResult] = []
    for step in steps:
        measured, n = _measure(series, step, offset)
        result = StepResult(step=step, measured=measured, n_samples=n)

        if math.isnan(step.value):
            result.note = "эталон не определён — шаг пропущен"
        elif measured is None:
            result.note = f"в окне шага только {n} точек лога"
        else:
            try:
                result.tolerance = tolerance(step.quantity, step.value, freq_hint)
            except UnknownFunction as exc:
                result.note = str(exc)
        results.append(result)

    return Report(results=results, offset=offset, scenario=scenario)
