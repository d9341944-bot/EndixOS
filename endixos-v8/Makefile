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
OBJ    := $(BUILD)/boot/boot.o $(filter-out $(BUILD)/boot/boot.o,$(ASMOBJ)) $(COBJ)
KERNEL := $(BUILD)/endixos.elf
ISO    := $(BUILD)/endixos.iso

# KVM, если доступен, иначе программная эмуляция. Мышь захватывается при наведении
# (выйти из захвата: Ctrl+Alt+G). Часы показывают местное время.
QEMU      := qemu-system-i386
QEMU_OPTS := -m 128 -serial stdio -rtc base=localtime \
             -nic user,model=e1000 -display gtk,grab-on-hover=on -accel kvm -accel tcg

.PHONY: all run iso check clean

all: $(KERNEL)

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
	@mkdir -p $(BUILD)/iso/boot/grub
	cp $(KERNEL) $(BUILD)/iso/boot/endixos.elf
	cp grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(BUILD)/iso

DISK := disk.img

# Виртуальный диск для сохранения настроек (лежит рядом с проектом: создаётся один раз и не стирается make clean)
$(DISK):
	dd if=/dev/zero of=$@ bs=512 count=2048 2>/dev/null

run: iso $(DISK)
	$(QEMU) -cdrom $(ISO) -boot d -drive file=$(DISK),format=raw,if=ide,index=0 $(QEMU_OPTS)

clean:
	rm -rf $(BUILD)
