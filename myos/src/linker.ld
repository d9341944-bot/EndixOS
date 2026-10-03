ENTRY(_start)

SECTIONS {
    . = 1M;

    kernel_start = .;

    .text : { *(.multiboot) *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(COMMON) *(.bss) }

    kernel_end = .;
}
