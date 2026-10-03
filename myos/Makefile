CC      := gcc
CFLAGS  := -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-misleading-indentation -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-sse2 -mno-mmx -mno-avx -mno-3dnow -mpreferred-stack-boundary=2 -Wall -Wextra -O2 -nostdlib
LDFLAGS := -m elf_i386 -T src/linker.ld -nostdlib -no-pie

OBJS := src/boot.o src/kernel.o src/tty.o src/gdt.o src/gdt_flush.o src/idt.o src/idt_load.o src/isr.o src/isr_stubs.o src/pic.o src/pit.o src/irq.o src/keyboard.o src/pmm.o src/paging.o src/heap.o src/framebuffer.o src/serial.o src/thread.o src/switch.o src/user.o src/syscall.o src/elf.o src/ata.o src/fat16.o src/fd.o src/mouse.o src/wm.o src/files.o src/settings.o src/rtc.o src/cmatrix.o src/launchpad.o src/anim.o src/terminal.o src/snake.o src/menu.o src/pci.o src/usb.o src/arp.o src/icmp.o src/dns.o src/tcp.o src/udp.o src/ip.o src/net.o src/rtl8139.o src/e1000.o

all: endix.iso

src/%.o: src/%.asm
	nasm -f elf32 $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

endix.elf: $(OBJS) src/linker.ld
	ld $(LDFLAGS) -o $@ $(OBJS)

user/build/shell.elf:
	$(MAKE) -C user

user/prog/hello.elf:
	$(MAKE) -C user/prog

endix.iso: endix.elf grub.cfg user/build/shell.elf user/prog/hello.elf
	mkdir -p iso/boot/grub
	cp endix.elf iso/boot/endix.elf
	cp user/build/shell.elf iso/boot/shell.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o endix.iso iso -volid ENDIXOS

run: endix.iso
	qemu-system-i386 -boot d -cdrom endix.iso -drive file=disk.img,format=raw,if=ide -nic user,model=rtl8139 -rtc base=localtime -display gtk -vga std -serial stdio

clean:
	rm -rf src/*.o endix.elf endix.iso iso
	$(MAKE) -C user clean
	$(MAKE) -C user/prog clean

.PHONY: all run clean
