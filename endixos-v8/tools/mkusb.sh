#!/usr/bin/env bash
# Загрузочная флешка EndixOS (BIOS/Legacy): MBR + активный раздел FAT32 + GRUB. Работает и в fish: просто ./tools/mkusb.sh
#   ./tools/mkusb.sh              выбрать флешку из списка и записать с нуля (стирает флешку целиком!)
#   ./tools/mkusb.sh /dev/sdX     то же для указанной флешки
#   ./tools/mkusb.sh --refresh    быстро заменить только ядро на уже готовой флешке (после каждой новой сборки)
#   ./tools/mkusb.sh --dry-run    ничего не менять, только показать, что будет сделано
set -euo pipefail
cd "$(dirname "$0")/.."

REFRESH=0; DRY=0; DEV=""
for a in "$@"; do case "$a" in --refresh) REFRESH=1;; --dry-run) DRY=1;; -h|--help) sed -n 2,6p "$0"; exit 0;; /dev/*) DEV="$a";; *) echo "Непонятный параметр: $a"; exit 1;; esac; done

run() { if [ "$DRY" = 1 ]; then echo "  [dry-run] $*"; else "$@"; fi; }
die() { echo "Ошибка: $*" >&2; exit 1; }

need() { for t in "$@"; do command -v "$t" >/dev/null || die "не хватает '$t'. На Arch/CachyOS: sudo pacman -S --needed parted dosfstools grub util-linux"; done; }
need lsblk parted mkfs.fat grub-install wipefs partprobe
[ "$DRY" = 1 ] || command -v sudo >/dev/null || die "нужен sudo"

# 1. свежая сборка (make сам пересобирает при смене заголовков)
echo "== Сборка ядра"
make -s
[ -f build/endixos.elf ] || die "нет build/endixos.elf"
BUILD=$(grep -o 'ENDIX_BUILD [0-9]*' gui/settings.h | head -1 | cut -d' ' -f2)
VER=$(grep -o 'ENDIX_VERSION "[^"]*"' gui/settings.h | head -1 | cut -d'"' -f2)
echo "   EndixOS $VER, build $BUILD, $(stat -c %s build/endixos.elf) байт"

# 2. выбор флешки: только съёмные диски, никогда системный
# Прошивка Wi-Fi (rtl8192cfw.bin, пакет linux-firmware): кладётся на флешку, если нашлась. Файл не входит в проект.
put_fw() {
    local f="" t
    for c in assets/firmware/rtl8192cfw.bin /usr/lib/firmware/rtlwifi/rtl8192cfw.bin /lib/firmware/rtlwifi/rtl8192cfw.bin; do [ -f "$c" ] && { f="$c"; break; }; done
    if [ -z "$f" ] && [ -f /usr/lib/firmware/rtlwifi/rtl8192cfw.bin.zst ] && command -v zstd >/dev/null; then
        t=$(mktemp); zstd -dqc /usr/lib/firmware/rtlwifi/rtl8192cfw.bin.zst > "$t"; f="$t"
    fi
    if [ -n "$f" ]; then run sudo cp "$f" "$MNT/boot/rtl8192c.bin"; echo "   Wi-Fi: прошивка rtl8192cfw.bin скопирована"
    else echo "   Wi-Fi: rtl8192cfw.bin не найдена (нужен пакет linux-firmware), команда wifi fw работать не будет"; fi
}

removable() { lsblk -dno NAME,RM,TYPE | awk '$2==1 && $3=="disk"{print "/dev/"$1}'; }
if [ -z "$DEV" ]; then
    mapfile -t LIST < <(removable)
    [ "${#LIST[@]}" -gt 0 ] || die "съёмных дисков не найдено. Вставь флешку и повтори."
    echo "== Съёмные диски:"
    i=1; for d in "${LIST[@]}"; do echo "   $i) $d  $(lsblk -dno SIZE,MODEL "$d" | xargs)"; i=$((i+1)); done
    if [ "${#LIST[@]}" -eq 1 ]; then DEV="${LIST[0]}"; else read -rp "Номер флешки: " n; DEV="${LIST[$((n-1))]}"; fi
fi
[ -b "$DEV" ] || die "$DEV не блочное устройство"
[ -d "/sys/block/$(basename "$DEV")" ] || die "нужен диск целиком (например /dev/sdb), а не раздел $DEV"
[ "$(cat /sys/block/"$(basename "$DEV")"/removable 2>/dev/null)" = 1 ] || die "$DEV не съёмное устройство: отказываюсь писать (защита системного диска)"
for m in $(lsblk -lno MOUNTPOINT "$DEV" | grep -E '^/($|boot|home|usr|var)' || true); do die "на $DEV смонтирован системный путь $m"; done

PART="${DEV}1"; case "$DEV" in *nvme*|*mmcblk*) PART="${DEV}p1";; esac
MNT=$(mktemp -d)
cleanup() { sudo umount "$MNT" 2>/dev/null || true; rmdir "$MNT" 2>/dev/null || true; }
trap cleanup EXIT

# 3a. быстрое обновление ядра
if [ "$REFRESH" = 1 ]; then
    echo "== Обновляю ядро на $DEV"
    run sudo mount "$PART" "$MNT"
    [ "$DRY" = 1 ] || [ -f "$MNT/boot/endixos.elf" ] || die "на $PART нет /boot/endixos.elf: флешка не подготовлена. Запусти без --refresh."
    run sudo cp build/endixos.elf "$MNT/boot/endixos.elf"
    run sudo cp grub.cfg "$MNT/boot/grub/grub.cfg"
put_fw
    run sync
    [ "$DRY" = 1 ] || cmp -s build/endixos.elf "$MNT/boot/endixos.elf" || die "ядро на флешке отличается от собранного"
    echo "Готово: на флешке build $BUILD."
    exit 0
fi

# 3b. запись с нуля
echo "== Будет СТЁРТА флешка: $DEV  $(lsblk -dno SIZE,MODEL "$DEV" | xargs)"
if [ "$DRY" != 1 ]; then read -rp "Напиши имя устройства ($DEV) для подтверждения: " ans; [ "$ans" = "$DEV" ] || die "не подтверждено"; fi
run sudo umount "${DEV}"?* 2>/dev/null || true
run sudo wipefs -a "$DEV"
run sudo parted -s "$DEV" mklabel msdos mkpart primary fat32 1MiB 100% set 1 boot on
run sudo partprobe "$DEV"; [ "$DRY" = 1 ] || sleep 2
run sudo mkfs.fat -F32 "$PART"
run sudo mount "$PART" "$MNT"
run sudo mkdir -p "$MNT/boot/grub"
run sudo cp build/endixos.elf "$MNT/boot/endixos.elf"
run sudo cp grub.cfg "$MNT/boot/grub/grub.cfg"
put_fw
run sudo grub-install --target=i386-pc --boot-directory="$MNT/boot" --modules="part_msdos fat" "$DEV"
run sync
[ "$DRY" = 1 ] || cmp -s build/endixos.elf "$MNT/boot/endixos.elf" || die "ядро на флешке отличается от собранного"
echo "Готово: загрузочная флешка с EndixOS $VER (build $BUILD)."
echo "На нетбуке: F12/Esc -> USB без слова UEFI. Дальше обновляй ядро быстро: ./tools/mkusb.sh --refresh"
