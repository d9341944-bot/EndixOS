ENTRY(_start)

SECTIONS {
    . = 0x03000000;

    .text   : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(COMMON) *(.bss*) }
}
