; ---- Multiboot header ----
MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000004          ; bit 2 = VIDEO_MODE request
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    ; 5 reserved dwords (a.out kludge, не используются, но должны быть на месте)
    dd 0        ; header_addr
    dd 0        ; load_addr
    dd 0        ; load_end_addr
    dd 0        ; bss_end_addr
    dd 0        ; entry_addr
    ; video request
    dd 0        ; mode_type = 0 (linear graphics)
    dd 1024     ; width
    dd 768      ; height
    dd 32       ; depth (bpp)

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kernel_main

_start:
    cli
    mov esp, stack_top
    xor ebp, ebp
    push ebx
    push eax
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
