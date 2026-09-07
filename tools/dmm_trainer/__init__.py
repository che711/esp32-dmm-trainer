# SPDX-License-Identifier: MIT
"""Инструменты стенда DMM Trainer: сверка логов Brymen BM788BT с эталоном."""

from .grading import Report, Step, StepResult, grade, find_offset, steps_from_truth
from .readers import Sample, Series, read_meter_csv, read_truth_csv
from .spec import tolerance, pick_range

__version__ = "0.3.0"

__all__ = [
    "Report", "Step", "StepResult", "grade", "find_offset", "steps_from_truth",
    "Sample", "Series", "read_meter_csv", "read_truth_csv",
    "tolerance", "pick_range", "__version__",
]
