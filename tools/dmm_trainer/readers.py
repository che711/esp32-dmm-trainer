# SPDX-License-Identifier: MIT
"""Чтение двух источников: CSV из приложения Brymen и truth.csv со стенда.

Формат экспорта у приложения Brymen меняется от версии к версии, поэтому
парсер намеренно терпимый: он ищет колонку времени и колонку значения по
набору синонимов, понимает запятую как десятичный разделитель и приводит
единицы (mV, kOhm, µA) к базовым. Если автоопределение промахнулось —
колонки задаются явно.
"""

from __future__ import annotations

import csv
import io
import re
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from pathlib import Path
from typing import Iterable, Optional, Sequence

__all__ = ["Sample", "Series", "read_meter_csv", "read_truth_csv", "parse_value"]

_TIME_KEYS = ("time", "timestamp", "datetime", "date/time", "время", "t_ms", "elapsed")
_VALUE_KEYS = ("value", "reading", "measure", "measurement", "data", "значение", "primary")
_UNIT_KEYS = ("unit", "units", "единица", "uom")

_TIME_FORMATS = (
    "%Y-%m-%d %H:%M:%S.%f",
    "%Y-%m-%d %H:%M:%S",
    "%Y/%m/%d %H:%M:%S.%f",
    "%Y/%m/%d %H:%M:%S",
    "%d.%m.%Y %H:%M:%S",
    "%H:%M:%S.%f",
    "%H:%M:%S",
)

_PREFIXES = {
    "p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "μ": 1e-6,
    "m": 1e-3, "": 1.0, "k": 1e3, "K": 1e3, "M": 1e6, "G": 1e9,
}

_VALUE_RE = re.compile(
    r"^\s*(?P<sign>[-+]?)\s*(?P<num>\d+(?:[.,]\d+)?)\s*"
    r"(?P<prefix>[pnuµμmkKMG]?)(?P<unit>[A-Za-zΩ°%]*)\s*$"
)


@dataclass(frozen=True)
class Sample:
    t: float  # секунды от начала ряда
    value: float  # базовые единицы (В, Ом, А, °C, %)
    raw: str = ""


@dataclass
class Series:
    samples: list[Sample] = field(default_factory=list)
    unit: str = ""
    source: str = ""

    def __len__(self) -> int:
        return len(self.samples)

    def __iter__(self) -> Iterable[Sample]:
        return iter(self.samples)

    @property
    def duration(self) -> float:
        return self.samples[-1].t - self.samples[0].t if len(self.samples) > 1 else 0.0

    def window(self, t_from: float, t_to: float) -> list[Sample]:
        return [s for s in self.samples if t_from <= s.t <= t_to]


class ParseError(ValueError):
    pass


def parse_value(text: str, unit_hint: str = "") -> tuple[float, str]:
    """'12,345 mV' -> (0.012345, 'V'). Возвращает значение и базовую единицу."""
    text = (text or "").strip()
    if not text or text.upper() in {"OL", "O.L", "-----", "----"}:
        return float("nan"), unit_hint

    m = _VALUE_RE.match(text)
    if not m:
        # чистое число без единиц
        try:
            return float(text.replace(",", ".")), unit_hint
        except ValueError as exc:
            raise ParseError(f"не разобрать значение {text!r}") from exc

    number = float(m.group("num").replace(",", "."))
    if m.group("sign") == "-":
        number = -number

    prefix = m.group("prefix")
    unit = m.group("unit") or unit_hint

    # «m» в «mV» — приставка, но в «Ohm» — часть слова
    if prefix and not unit and unit_hint:
        unit = unit_hint
    scale = _PREFIXES.get(prefix, 1.0)
    return number * scale, unit


def _find_column(header: Sequence[str], keys: Sequence[str]) -> Optional[int]:
    lowered = [h.strip().lower() for h in header]
    for i, name in enumerate(lowered):
        if any(k == name for k in keys):
            return i
    for i, name in enumerate(lowered):
        if any(k in name for k in keys):
            return i
    return None


def _parse_time(text: str) -> Optional[datetime]:
    text = text.strip()
    for fmt in _TIME_FORMATS:
        try:
            return datetime.strptime(text, fmt)
        except ValueError:
            continue
    return None


def read_meter_csv(
    path: str | Path,
    time_col: Optional[int] = None,
    value_col: Optional[int] = None,
    unit_col: Optional[int] = None,
    delimiter: Optional[str] = None,
) -> Series:
    """CSV-лог мультиметра (экспорт приложения Brymen BLE-Comm)."""
    raw = Path(path).read_text(encoding="utf-8-sig", errors="replace")
    if delimiter is None:
        try:
            delimiter = csv.Sniffer().sniff(raw[:4096], delimiters=",;\t").delimiter
        except csv.Error:
            delimiter = ","

    rows = list(csv.reader(io.StringIO(raw), delimiter=delimiter))
    rows = [r for r in rows if any(cell.strip() for cell in r)]
    if not rows:
        raise ParseError(f"{path}: пустой файл")

    header = rows[0]
    has_header = any(not _looks_numeric(c) for c in header)
    body = rows[1:] if has_header else rows

    if time_col is None:
        time_col = _find_column(header, _TIME_KEYS) if has_header else 0
    if value_col is None:
        value_col = _find_column(header, _VALUE_KEYS) if has_header else 1
    if unit_col is None and has_header:
        unit_col = _find_column(header, _UNIT_KEYS)

    if time_col is None or value_col is None:
        raise ParseError(
            f"{path}: не найдены колонки времени/значения. "
            f"Заголовок: {header}. Задайте --time-col/--value-col."
        )

    samples: list[Sample] = []
    unit = ""
    t0: Optional[datetime] = None
    for row in body:
        if max(time_col, value_col) >= len(row):
            continue
        t_raw = row[time_col].strip()
        v_raw = row[value_col].strip()
        unit_hint = row[unit_col].strip() if unit_col is not None and unit_col < len(row) else ""

        stamp = _parse_time(t_raw)
        if stamp is not None:
            if t0 is None:
                t0 = stamp
            delta = stamp - t0
            if delta < timedelta(0):  # переход через полночь
                delta += timedelta(days=1)
            t = delta.total_seconds()
        else:
            try:
                t = float(t_raw.replace(",", "."))
            except ValueError:
                continue
            if t > 1e6:  # похоже на миллисекунды
                t /= 1000.0
            if not samples:
                t0_offset = t
                t = 0.0
            else:
                t -= 0.0

        try:
            value, parsed_unit = parse_value(v_raw, unit_hint)
        except ParseError:
            continue
        unit = unit or parsed_unit
        samples.append(Sample(t=t, value=value, raw=v_raw))

    if not samples:
        raise ParseError(f"{path}: не удалось прочитать ни одной точки")

    # нормализуем начало отсчёта
    base = samples[0].t
    samples = [Sample(s.t - base, s.value, s.raw) for s in samples]
    return Series(samples=samples, unit=unit, source=str(path))


def _looks_numeric(cell: str) -> bool:
    try:
        float(cell.strip().replace(",", "."))
        return True
    except ValueError:
        return False


@dataclass
class TruthPoint:
    t: float
    step: int
    quantity: str
    value: float


def read_truth_csv(path: str | Path) -> list[TruthPoint]:
    """truth.csv со стенда: t_ms,step,quantity,value."""
    points: list[TruthPoint] = []
    with Path(path).open(encoding="utf-8-sig") as fh:
        for row in csv.DictReader(fh):
            try:
                points.append(
                    TruthPoint(
                        t=float(row["t_ms"]) / 1000.0,
                        step=int(row["step"]),
                        quantity=row["quantity"].strip(),
                        value=float(row["value"]),
                    )
                )
            except (KeyError, ValueError):
                continue
    if not points:
        raise ParseError(f"{path}: ground-truth лог пуст или повреждён")
    return points
