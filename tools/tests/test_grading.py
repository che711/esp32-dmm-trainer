# SPDX-License-Identifier: MIT
"""Тесты сверки. Синтетические логи проверяют, что оценка не врёт
ни в сторону «всё хорошо», ни в сторону ложных провалов."""

from __future__ import annotations

import math
import random

import pytest

from dmm_trainer.grading import find_offset, grade, steps_from_truth
from dmm_trainer.readers import ParseError, Sample, Series, TruthPoint, parse_value, read_meter_csv
from dmm_trainer.spec import pick_range, tolerance


# ------------------------------------------------------------------ spec
def test_dcv_tolerance_matches_datasheet():
    # 2,5 В на пределе 6 В: 0,03% + 2 единицы по 0,1 мВ
    assert tolerance("dcv", 2.5) == pytest.approx(2.5 * 0.0003 + 2 * 1e-4)


def test_dcv_range_selection():
    assert pick_range("dcv", 0.5).full_scale == 0.6
    assert pick_range("dcv", 5.0).full_scale == 6.0
    assert pick_range("dcv", 230.0).full_scale == 600.0
    assert pick_range("dcv", 900.0).full_scale == 1000.0


def test_acv_tolerance_grows_with_frequency():
    at_50hz = tolerance("acv", 2.0, freq=50)
    at_5khz = tolerance("acv", 2.0, freq=5000)
    at_50khz = tolerance("acv", 2.0, freq=50000)
    assert at_50hz < at_5khz < at_50khz


def test_ohm_tolerance_uses_correct_range():
    # 1 кОм попадает на предел 6 кОм: 0,085% + 4 единицы по 0,1 Ом
    assert tolerance("ohm", 1000.0) == pytest.approx(1000 * 0.00085 + 4 * 0.1)


def test_milliamp_tolerance_returned_in_milliamps():
    # 12 мА -> предел 60 мА, 0,075% + 20 единиц по 0,001 мА
    assert tolerance("ma", 12.0) == pytest.approx(12 * 0.00075 + 20 * 1e-3, rel=1e-6)


def test_duty_tolerance_scales_with_frequency():
    assert tolerance("duty", 25.0, freq=1000) > tolerance("duty", 25.0, freq=10)


def test_unknown_quantity_raises():
    with pytest.raises(ValueError):
        tolerance("magnetism", 1.0)


# --------------------------------------------------------------- readers
@pytest.mark.parametrize(
    "text,expected",
    [
        ("12,345 mV", 0.012345),
        ("1.234 V", 1.234),
        ("-0,5 V", -0.5),
        ("47 kOhm", 47000.0),  # 'k' + 'Ohm': приставка отделяется от единицы
        ("230.4", 230.4),
    ],
)
def test_parse_value(text, expected):
    value, _ = parse_value(text)
    assert value == pytest.approx(expected)


def test_parse_value_handles_overload():
    value, _ = parse_value("OL")
    assert math.isnan(value)


def test_read_meter_csv_with_timestamps(tmp_path):
    path = tmp_path / "meter.csv"
    path.write_text(
        "Time,Value,Unit\n"
        "2026-05-01 10:00:00,2.4998,V\n"
        "2026-05-01 10:00:01,2.4999,V\n"
        "2026-05-01 10:00:02,2.5001,V\n",
        encoding="utf-8",
    )
    series = read_meter_csv(path)
    assert len(series) == 3
    assert series.samples[0].t == 0.0
    assert series.samples[-1].t == pytest.approx(2.0)
    assert series.samples[0].value == pytest.approx(2.4998)


def test_read_meter_csv_semicolon_and_comma_decimal(tmp_path):
    path = tmp_path / "meter.csv"
    path.write_text(
        "Timestamp;Reading;Units\n"
        "10:00:00;2,4998;V\n"
        "10:00:01;2,5002;V\n",
        encoding="utf-8",
    )
    series = read_meter_csv(path)
    assert len(series) == 2
    assert series.samples[1].value == pytest.approx(2.5002)


def test_read_meter_csv_rejects_garbage(tmp_path):
    path = tmp_path / "empty.csv"
    path.write_text("\n\n", encoding="utf-8")
    with pytest.raises(ParseError):
        read_meter_csv(path)


# --------------------------------------------------------------- grading
def make_truth(values, quantity="dcv", step_seconds=20, period=1.0):
    """Ground-truth лог: каждое значение держится step_seconds секунд."""
    points = []
    t = 0.0
    for i, v in enumerate(values):
        for _ in range(int(step_seconds / period)):
            points.append(TruthPoint(t=t, step=i, quantity=quantity, value=v))
            t += period
    return points


def make_meter(points, error=0.0, offset=0.0, noise=0.0, seed=1):
    """Идеальный мультиметр с заданной систематической ошибкой и шумом."""
    rng = random.Random(seed)
    samples = [
        Sample(
            t=p.t + offset,
            value=p.value * (1 + error) + rng.uniform(-noise, noise),
        )
        for p in points
    ]
    return Series(samples=samples)


def test_perfect_meter_passes():
    points = make_truth([2.5, 1.0, 3.0])
    series = make_meter(points)
    report = grade(series, points, offset=0.0)
    assert report.ok
    assert report.passed == 3
    assert report.failed == 0


def test_meter_outside_spec_fails():
    points = make_truth([2.5, 1.0, 3.0])
    series = make_meter(points, error=0.01)  # 1% — далеко за 0,03%
    report = grade(series, points, offset=0.0)
    assert report.failed == 3
    assert not report.ok


def test_meter_just_inside_spec_passes():
    points = make_truth([2.5])
    # допуск на 2,5 В ~0,95 мВ; берём ошибку 0,5 мВ
    series = make_meter(points, error=0.0002)
    report = grade(series, points, offset=0.0)
    assert report.ok


def test_offset_is_recovered():
    points = make_truth([2.5, 1.0, 3.0], step_seconds=30)
    series = make_meter(points, offset=17.0)
    steps = steps_from_truth(points)
    assert find_offset(series, steps, search=60) == pytest.approx(17.0, abs=0.5)


def test_grade_finds_offset_automatically():
    points = make_truth([2.5, 1.0, 3.0], step_seconds=30)
    series = make_meter(points, offset=-12.0)
    report = grade(series, points)
    assert report.offset == pytest.approx(-12.0, abs=0.5)
    assert report.ok


def test_nan_truth_is_skipped_not_failed():
    points = make_truth([2.5, float("nan")])
    series = make_meter(points)
    report = grade(series, points, offset=0.0)
    assert report.skipped == 1
    assert report.passed == 1
    assert report.failed == 0


def test_missing_meter_data_is_reported():
    points = make_truth([2.5, 1.0])
    series = Series(samples=[])  # мультиметр молчал
    report = grade(series, points, offset=0.0)
    assert report.passed == 0
    assert all("точек лога" in r.note for r in report.results)


def test_steps_from_truth_boundaries():
    points = make_truth([1.0, 2.0], step_seconds=10, period=1.0)
    steps = steps_from_truth(points)
    assert len(steps) == 2
    assert steps[0].t_start == 0.0
    assert steps[0].t_end == pytest.approx(10.0)
    assert steps[1].value == 2.0


def test_settle_window_ignores_transient():
    """Первая треть шага отбрасывается — переходный процесс не должен валить тест."""
    points = make_truth([2.5], step_seconds=30)
    samples = []
    for p in points:
        # первые 8 секунд прибор показывает мусор
        value = 0.0 if p.t < 8 else 2.5
        samples.append(Sample(t=p.t, value=value))
    report = grade(Series(samples=samples), points, offset=0.0)
    assert report.ok
