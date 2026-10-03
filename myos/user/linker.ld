ENTRY(_start)

SECTIONS {
    . = 0x02000000;

    .text   : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(COMMON) *(.bss*) }
}
