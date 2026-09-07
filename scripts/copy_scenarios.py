"""Кладёт scenarios/*.json в образ файловой системы перед сборкой.

Сценарии лежат в корне репозитория, чтобы их было удобно править и
ревьюить, а во флеш попадают как /scenarios внутри LittleFS.
"""

import shutil
from pathlib import Path

Import("env")  # noqa: F821 — внедряется PlatformIO

root = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
src = root / "scenarios"
dst = root / "data" / "scenarios"

if src.is_dir():
    dst.mkdir(parents=True, exist_ok=True)
    for item in dst.glob("*.json"):
        item.unlink()
    for item in src.glob("*.json"):
        shutil.copy2(item, dst / item.name)
    print(f"[scenarios] скопировано {len(list(src.glob('*.json')))} файлов в data/scenarios")
else:
    print("[scenarios] каталог scenarios/ не найден — пропускаю")
