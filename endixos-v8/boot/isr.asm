; Заглушки прерываний: 0..31 - исключения CPU, 32..47 - IRQ0..15 после ремапа PIC.
; Все заглушки приводят стек к одному виду и вызывают isr_handler_c(regs_t *).

bits 32

extern isr_handler_c

%macro ISR_NOERR 1
isr %+ %1:
    push dword 0            ; фиктивный код ошибки
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr %+ %1:
    push dword %1           ; код ошибки уже положил процессор
    jmp isr_common
%endmacro

section .text

%assign n 0
%rep 48
    %if (n == 8) || (n >= 10 && n <= 14) || (n == 17) || (n == 30)
        ISR_ERR n
    %else
        ISR_NOERR n
    %endif
    %assign n n+1
%endrep

isr_common:
    pushad
    cld
    push esp                ; аргумент: указатель на сохранённые регистры
    call isr_handler_c
    add esp, 4
    popad
    add esp, 8              ; снять номер прерывания и код ошибки
    iretd

section .data
global isr_stub_table
isr_stub_table:
%assign n 0
%rep 48
    dd isr %+ n
    %assign n n+1
%endrep
