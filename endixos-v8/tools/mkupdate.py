#!/usr/bin/env python3
"""Готовит папку update/ для GitHub: latest.txt (манифест) и endixos.elf.
Использование:  python3 tools/mkupdate.py 8.1 "Пункт 1|Пункт 2|Пункт 3"
Номер build берётся из ENDIX_BUILD в gui/settings.h; перед выпуском увеличь его на 1 и пересобери (make)."""
import hashlib, os, re, shutil, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if len(sys.argv) < 2:
    sys.exit(__doc__)
version = sys.argv[1]
notes = sys.argv[2] if len(sys.argv) > 2 else ""
build = int(re.search(r"#define ENDIX_BUILD (\d+)", open(os.path.join(root, "gui/settings.h")).read()).group(1))
elf = os.path.join(root, "build/endixos.elf")
if not os.path.exists(elf):
    sys.exit("Нет build/endixos.elf: сначала выполни make")
data = open(elf, "rb").read()
if len(data) >= 4 << 20:
    sys.exit("Ядро больше 4 МБ: увеличь UP_MAX в gui/update.c")
out = os.path.join(root, "update")
os.makedirs(out, exist_ok=True)
shutil.copyfile(elf, os.path.join(out, "endixos.elf"))
open(os.path.join(out, "latest.txt"), "w").write(
    f"build={build}\nversion={version}\nsize={len(data)}\nnotes={notes}\nsha256={hashlib.sha256(data).hexdigest()}\n")
print(f"update/: build {build}, версия {version}, {len(data)} байт. Закоммить папку update/ в ветку main, рядом с gui/ и kernel/ (в репозитории это endixos-v8/update/).")
