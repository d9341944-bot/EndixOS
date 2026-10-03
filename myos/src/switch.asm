section .text

global switch_to
; void switch_to(uint32_t* old_esp_slot, uint32_t new_esp)
switch_to:
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp + 20]     ; old_esp_slot (4 ret + 16 pushes = 20)
    mov [eax], esp          ; сохранить текущий esp

    mov eax, [esp + 24]     ; new_esp
    mov esp, eax            ; переключиться на стек нового потока

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global thread_start
extern thread_exit
thread_start:
    ; ebx = user function
    sti                     ; прерывания были выключены во время switch_to
    call ebx
    call thread_exit        ; если функция вернулась — завершаем поток
.hang:
    cli
    hlt
    jmp .hang
