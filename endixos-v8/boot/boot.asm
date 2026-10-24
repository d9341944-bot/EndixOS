; EndixOS v8 - точка входа (Multiboot 1, 32-bit)
; Просим у загрузчика линейный фреймбуфер 1024x768x32.

bits 32

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000007          ; бит0: выравнивание модулей, бит1: карта памяти, бит2: видеорежим
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    dd 0, 0, 0, 0, 0                ; поля адресов (не используются для ELF)
    dd 0                            ; mode_type: 0 = графика
    dd 1024                         ; ширина
    dd 768                          ; высота
    dd 32                           ; глубина цвета

section .bss
align 16
stack_bottom:
    resb 65536
stack_top:

section .text
global _start
extern kernel_main

_start:
    cli
    mov esp, stack_top
    push 0
    popf                            ; сбросить EFLAGS
    push ebx                        ; указатель на multiboot_info
    push eax                        ; magic
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
