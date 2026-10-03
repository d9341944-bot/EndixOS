CC      := gcc
CFLAGS  := -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -mno-sse -mno-sse2 -mno-mmx -mno-avx \
           -mpreferred-stack-boundary=2 -Wall -Wextra -O2 -nostdlib

all: hello.elf

../build/start.o: ../start.c
	$(MAKE) -C ..

build/hello.o: hello.c ../syscall.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

hello.elf: ../build/start.o build/hello.o prog.ld
	ld -m elf_i386 -T prog.ld -nostdlib -no-pie -o $@ ../build/start.o build/hello.o

clean:
	rm -rf build hello.elf

.PHONY: all clean
