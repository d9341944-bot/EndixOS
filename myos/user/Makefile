CC      := gcc
CFLAGS  := -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -mno-sse -mno-sse2 -mno-mmx -mno-avx \
           -mpreferred-stack-boundary=2 -Wall -Wextra -O2 -nostdlib

all: build/hello.elf build/shell.elf

build/start.o: start.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/hello.o: hello.c syscall.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/shell.o: shell.c syscall.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/hello.elf: build/start.o build/hello.o linker.ld
	ld -m elf_i386 -T linker.ld -nostdlib -no-pie -o $@ build/start.o build/hello.o

build/shell.elf: build/start.o build/shell.o linker.ld
	ld -m elf_i386 -T linker.ld -nostdlib -no-pie -o $@ build/start.o build/shell.o

clean:
	rm -rf build

.PHONY: all clean
