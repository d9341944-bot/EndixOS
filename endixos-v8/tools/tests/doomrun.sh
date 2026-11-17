#!/bin/sh
# Хост-тест Doom: tools/tests/doomrun.sh <wad>. Кадры: /tmp/doom_N.ppm
set -e; cd "$(dirname "$0")/../.."
tools/tests/doomcc.sh /tmp/doomobj
INC=$(gcc -m32 -print-file-name=include)
for f in kernel/string.c tools/tests/doomrun.c; do gcc -m32 -O2 -w -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -mgeneral-regs-only -nostdinc -isystem $INC -Idoom/libc -Idoom -Ikernel -c $f -o /tmp/doomobj/x_$(basename $f .c).o; done
ld -m elf_i386 -static -nostdlib -o /tmp/doomrun /tmp/doomobj/*.o
/tmp/doomrun "$1"
