#!/bin/sh
# Сборка движка Doom теми же флагами, что и ядро; печатает ошибки. Использование: doomcc.sh <каталог объектов>
cd "$(dirname "$0")/../../doom" || exit 1
O=${1:-/tmp/doomobj}; mkdir -p "$O"
INC=$(gcc -m32 -print-file-name=include)
FL="-std=gnu11 -O2 -w -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -fno-tree-loop-distribute-patterns -mgeneral-regs-only -mtune=bonnell -fno-asynchronous-unwind-tables -nostdinc -isystem $INC -Ilibc -I. -DDOOM_ARENA_MB=64 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200"
rc=0
for f in *.c; do gcc $FL -c $f -o $O/${f%.c}.o 2>&1 | head -5 && [ -f $O/${f%.c}.o ] || { echo "FAIL $f"; rc=1; }; done
exit $rc
