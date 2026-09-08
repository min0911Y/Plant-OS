[BITS 32]

section .text

extern kernel_lock_leave
extern x86_user_frame_set_tls

global arch_task_switch
global arch_task_start
global arch_task_interrupt_return

; cdecl arguments after establishing EBP in arch_task_switch:
;   [ebp + 8]  arch_task_context_t **current_context_slot
;   [ebp + 12] arch_task_context_t *next_context
;   [ebp + 16] uintptr_t next_cr3
;   [ebp + 20] struct mtask **scheduler_current_slot
;   [ebp + 24] struct mtask *next_task
; The pushes produce arch_task_context_t in eax..eip field order.
arch_task_switch:
  push ebp
  mov ebp, esp
  push edi
  push esi
  push edx
  push ecx
  push ebx
  push eax

  mov eax, [ebp + 8]
  mov [eax], esp
  mov eax, [ebp + 12]
  mov edx, [ebp + 16]
  mov ecx, [ebp + 20]
  mov ebx, [ebp + 24]

  ; The scheduler calls with interrupts already in the required state. Keep the
  ; current-task publication, stack switch and address-space switch contiguous.
  mov [ecx], ebx
  mov esp, eax
  mov cr3, edx

  pop eax
  pop ebx
  pop ecx
  pop edx
  pop esi
  pop edi
  pop ebp
  ret

; cdecl arguments at entry to arch_task_start:
;   [esp + 4]  arch_task_context_t *next_context
;   [esp + 8]  uintptr_t next_cr3
;   [esp + 12] struct mtask **scheduler_current_slot
;   [esp + 16] struct mtask *next_task
arch_task_start:
  mov eax, [esp + 4]
  mov edx, [esp + 8]
  mov ecx, [esp + 12]
  mov ebx, [esp + 16]

  mov [ecx], ebx
  mov esp, eax
  mov cr3, edx

  pop eax
  pop ebx
  pop ecx
  pop edx
  pop esi
  pop edi
  pop ebp
  ret

; Resume the x86_interrupt_frame_t copied by task_fork. This is intentionally
; byte-for-byte equivalent to the normal interrupt_entries.asm restore tail.
arch_task_interrupt_return:
  push esp
  call x86_user_frame_set_tls
  add esp, 4
  call kernel_lock_leave
  popa
  pop gs
  pop fs
  pop es
  pop ds
  iretd
