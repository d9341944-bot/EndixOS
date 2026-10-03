section .text
global enter_user
; void enter_user(uint32_t entry, uint32_t user_stack)
enter_user:
    mov eax, [esp+4]        ; entry point
    mov ebx, [esp+8]        ; user stack top

    mov cx, 0x23            ; user data (index 4 | RPL 3)
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    push 0x23               ; ss
    push ebx                ; esp
    pushfd                  ; eflags
    pop ecx
    or ecx, 0x200           ; enable IF
    push ecx
    push 0x1B               ; cs = user code (index 3 | RPL 3)
    push eax                ; eip
    iret
