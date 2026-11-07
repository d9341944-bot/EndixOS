# EndixOS v8
# Если есть i686-elf-gcc, используется он; иначе системный gcc с -m32.

ifneq ($(shell command -v i686-elf-gcc 2>/dev/null),)
CC := i686-elf-gcc
else
CC := gcc -m32
endif

AS      := nasm -f elf32
LD      := ld -m elf_i386
CFLAGS  := -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-pic -fno-pie \
           -fno-stack-protector -fno-builtin -fno-tree-loop-distribute-patterns \
           -mgeneral-regs-only -nostdlib
LDFLAGS := -T linker.ld -nostdlib -z noexecstack

BUILD  := build
CSRC   := $(shell find kernel drivers gui -name '*.c')
COBJ   := $(patsubst %.c,$(BUILD)/%.o,$(CSRC))
ASMSRC := $(wildcard boot/*.asm)
ASMOBJ := $(patsubst %.asm,$(BUILD)/%.o,$(ASMSRC))
BLOB   := $(BUILD)/blob
OBJ    := $(BUILD)/boot/boot.o $(filter-out $(BUILD)/boot/boot.o,$(ASMOBJ)) $(COBJ) $(BLOB)/boot_img.o $(BLOB)/core_img.o $(BLOB)/wall_pak.o
KERNEL := $(BUILD)/endixos.elf
ISO    := $(BUILD)/endixos.iso

# KVM, если доступен, иначе программная эмуляция. Мышь захватывается при наведении
# (выйти из захвата: Ctrl+Alt+G). Часы показывают местное время.
QEMU      := qemu-system-i386
QEMU_OPTS := -m 128 -serial stdio -rtc base=localtime \
             -nic user,model=e1000 -device intel-hda -device hda-duplex -display gtk,grab-on-hover=on -accel kvm -accel tcg

.PHONY: all run run-install run-installed iso usb usb-iso check clean

all: $(KERNEL)


# Загрузчик GRUB для установки на диск: boot.img (MBR) и core.img (со всеми нужными модулями внутри) вшиваются в ядро.
# Если GRUB на этом компьютере не установлен, образы пустые, а установщик скажет, чего не хватает.
GRUBLIB := $(firstword $(wildcard /usr/lib/grub/i386-pc /usr/lib32/grub/i386-pc /usr/share/grub/i386-pc))
GRUBMODS := biosdisk part_msdos fat normal configfile multiboot boot echo test vbe video video_fb vga all_video

$(BLOB)/boot.img:
	@mkdir -p $(BLOB)
	@if [ -n "$(GRUBLIB)" ] && [ -f $(GRUBLIB)/boot.img ]; then cp $(GRUBLIB)/boot.img $@; else printf x > $@; echo "note: GRUB boot.img not found, installer will have no bootloader"; fi

$(BLOB)/core.img:
	@mkdir -p $(BLOB)
	@if [ -n "$(GRUBLIB)" ] && command -v grub-mkimage >/dev/null 2>&1; then \
	  grub-mkimage -O i386-pc -d $(GRUBLIB) -o $@ -p '(hd0,msdos1)/boot/grub' $(GRUBMODS) || printf x > $@; \
	else printf x > $@; echo "note: grub-mkimage not found, installer will have no bootloader"; fi

$(BLOB)/%_img.o: $(BLOB)/%.img
	cd $(BLOB) && objcopy -I binary -O elf32-i386 -B i386 $*.img $*_img.o

$(BLOB)/wall_pak.o: assets/wall.pak
	@mkdir -p $(BLOB)
	cp assets/wall.pak $(BLOB)/wall.pak
	cd $(BLOB) && objcopy -I binary -O elf32-i386 -B i386 wall.pak wall_pak.o

$(KERNEL): $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Проверка Multiboot-заголовка (нужен grub-file)
check: $(KERNEL)
	grub-file --is-x86-multiboot $(KERNEL) && echo "multiboot: OK"

# ISO через GRUB (нужны grub, xorriso и mtools). GRUB выставляет графический режим,
# обычный 'qemu -kernel' этого не умеет.
iso: $(KERNEL)
	@for t in grub-mkrescue xorriso mformat; do command -v $$t >/dev/null || { echo "Не хватает '$$t'. На Arch/CachyOS: sudo pacman -S grub xorriso mtools"; exit 1; }; done
	@mkdir -p $(BUILD)/iso/boot/grub
	cp $(KERNEL) $(BUILD)/iso/boot/endixos.elf
	cp grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(BUILD)/iso

# Загрузочная флешка:  make usb DEV=/dev/sdX   (стирает ВСЮ флешку; проверь имя через lsblk!)
# Делает обычную флешку: таблица MBR, активный раздел FAT32, GRUB записан в MBR. Так грузятся даже старые BIOS (Phoenix и т. п.),
# которые не принимают гибридный ISO. Нужны: parted, dosfstools, grub.
usb: $(KERNEL)
	@test -n "$(DEV)" || { echo "Укажи флешку: make usb DEV=/dev/sdX  (список: lsblk)"; exit 1; }
	@test -b "$(DEV)" || { echo "$(DEV) не блочное устройство"; exit 1; }
	@case "$(DEV)" in *[0-9]) echo "Нужен диск целиком (например /dev/sdb), а не раздел $(DEV)"; exit 1;; esac
	@test "$$(cat /sys/block/$$(basename $(DEV))/removable 2>/dev/null)" = 1 || { echo "$(DEV) не съёмное устройство - отказываюсь писать (защита системного диска)"; exit 1; }
	@for t in parted mkfs.fat grub-install; do command -v $$t >/dev/null || { echo "Не хватает '$$t'. На Arch/CachyOS: sudo pacman -S parted dosfstools grub"; exit 1; }; done
	-sudo umount $(DEV)* 2>/dev/null
	sudo wipefs -a $(DEV)
	sudo parted -s $(DEV) mklabel msdos mkpart primary fat32 1MiB 100% set 1 boot on
	sudo partprobe $(DEV); sleep 2
	sudo mkfs.fat -F32 $(DEV)1
	@m=$$(mktemp -d) && \
	sudo mount $(DEV)1 $$m && \
	sudo mkdir -p $$m/boot/grub && \
	sudo cp $(KERNEL) $$m/boot/endixos.elf && \
	sudo cp grub.cfg $$m/boot/grub/grub.cfg && \
	sudo grub-install --target=i386-pc --boot-directory=$$m/boot --modules="part_msdos fat" $(DEV) && \
	sync && sudo umount $$m && rmdir $$m
	@echo "Готово: загрузись с флешки (USB HDD в BIOS)."

# Запасной вариант: записать готовый гибридный ISO как есть
usb-iso: iso
	@test -b "$(DEV)" || { echo "Укажи флешку: make usb-iso DEV=/dev/sdX"; exit 1; }
	-sudo umount $(DEV)* 2>/dev/null
	sudo dd if=$(ISO) of=$(DEV) bs=4M conv=fsync status=progress
	sync

DISK := disk.img

# Виртуальный диск для сохранения настроек (лежит рядом с проектом: создаётся один раз и не стирается make clean)
$(DISK):
	dd if=/dev/zero of=$@ bs=512 count=2048 2>/dev/null

run: iso $(DISK)
	$(QEMU) -cdrom $(ISO) -boot d -drive file=$(DISK),format=raw,if=ide,index=0 $(QEMU_OPTS)

# Виртуальный SATA-диск (AHCI) для проверки установщика; пустой, 1 ГБ, место занимает по мере записи
TARGET := target.img
$(TARGET):
	truncate -s 1G $@

# Загрузка с ISO, на втором диске (AHCI) ставим систему: Apple menu > Install EndixOS...
run-install: iso $(DISK) $(TARGET)
	$(QEMU) -cdrom $(ISO) -boot d -drive file=$(DISK),format=raw,if=ide,index=0 \
		-device ich9-ahci,id=ahci -drive id=t0,file=$(TARGET),format=raw,if=none -device ide-hd,drive=t0,bus=ahci.0 $(QEMU_OPTS)

# Загрузка уже установленной системы с target.img (без ISO): GRUB с диска, экран входа
run-installed: $(TARGET)
	$(QEMU) -device ich9-ahci,id=ahci -drive id=t0,file=$(TARGET),format=raw,if=none -device ide-hd,drive=t0,bus=ahci.0 -boot c $(QEMU_OPTS)

clean:
	rm -rf $(BUILD)

# wm.c включает .inc-файлы: без этой строки правка в них не пересобирает wm.o
$(BUILD)/gui/wm.o: $(wildcard gui/*.inc)
