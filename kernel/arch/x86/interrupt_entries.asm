[BITS 32]
section .data
GLOBAL asm_inthandler21, asm_inthandler20
EXTERN inthandler21, inthandler20, inthandler2c, signal_deal
EXTERN x86_syscall_dispatch
EXTERN kernel_lock_enter, kernel_lock_leave
EXTERN scheduler_reschedule_interrupt
GLOBAL x86_syscall_entry
GLOBAL x86_reschedule_entry
GLOBAL asm_inthandler2c, floppy_int
section .text
global null_inthandler
%define PDE_ADDRESS 0x400000
null_inthandler:
	IRETD

x86_syscall_entry:
  push ds
  push es
  push fs
  push gs
  pusha
  mov eax, esp
  push eax
  mov ax, ss
  mov ds, ax
  mov es, ax
  call kernel_lock_enter
  call x86_syscall_dispatch
  add esp, 4
  call signal_deal
  call kernel_lock_leave
  popa
  pop gs
  pop fs
  pop es
  pop ds
  iretd

extern flint
floppy_int:
	push ds
  push es
  push fs
  push gs
  pusha
	PUSH	ES
	PUSH	DS
	PUSHAD
	MOV		EAX,ESP
	PUSH	EAX
	MOV		AX,SS
	MOV		DS,AX
	MOV		ES,AX
	CALL kernel_lock_enter
	CALL	flint
	POP		EAX
	CALL kernel_lock_leave
	;call signal_deal
	POPAD
	POP		DS
	POP		ES
  popa
  pop gs
  pop fs
  pop es
  pop ds
	IRETD

EXTERN PCNET_IRQ
GLOBAL PCNET_ASM_INTHANDLER
PCNET_ASM_INTHANDLER:
	push ds
  push es
  push fs
  push gs
  pusha
  mov ax, ss
  mov ds, ax
  mov es, ax
  call kernel_lock_enter
  call PCNET_IRQ
  call kernel_lock_leave
  popa
  pop gs
  pop fs
  pop es
  pop ds
	IRETD

EXTERN RTL8139_IRQ
GLOBAL RTL8139_ASM_INTHANDLER
RTL8139_ASM_INTHANDLER:
	push ds
  push es
  push fs
  push gs
  pusha
  mov ax, ss
  mov ds, ax
  mov es, ax
  call kernel_lock_enter
  call RTL8139_IRQ
  call kernel_lock_leave
  popa
  pop gs
  pop fs
  pop es
  pop ds
	IRETD

EXTERN sb16_handler
GLOBAL asm_sb16_handler,asm_rtc_handler
asm_sb16_handler:
	push ds
  push es
  push fs
  push gs
  pusha
	PUSH	ES
	PUSH	DS
	PUSHAD
	MOV		EAX,ESP
	PUSH	EAX
	MOV		AX,SS
	MOV		DS,AX
	MOV		ES,AX
	CALL kernel_lock_enter
	CALL	sb16_handler
	POP		EAX
	CALL kernel_lock_leave
	;call signal_deal
	POPAD
	POP		DS
	POP		ES
  popa
  pop gs
  pop fs
  pop es
  pop ds
	IRETD
EXTERN ide_irq,rtc_irq
GLOBAL asm_ide_irq
asm_ide_irq:
	push ds
  push es
  push fs
  push gs
  pusha
	PUSH	ES
	PUSH	DS
	PUSHAD
	MOV		EAX,ESP
	PUSH	EAX
	MOV		AX,SS
	MOV		DS,AX
	MOV		ES,AX
	CALL kernel_lock_enter
	CALL	ide_irq
	POP		EAX
	CALL kernel_lock_leave
	;call signal_deal
	POPAD
	POP		DS
	POP		ES
  popa
  pop gs
  pop fs
  pop es
  pop ds
	IRETD
asm_rtc_handler:
	push ds
  push es
  push fs
  push gs
  pusha
	mov ax, ss
	mov ds, ax
	mov es, ax
	call kernel_lock_enter
	call rtc_irq
	call kernel_lock_leave
  popa
  pop gs
  pop fs
  pop es
  pop ds
		IRETD
asm_inthandler20:
  push ds
  push es
  push fs
  push gs
  pusha
	PUSH	ES
	PUSH	DS
	PUSHAD
	MOV		EAX,ESP
	add EAX,40
	PUSH	EAX
	MOV		AX,SS
	MOV		DS,AX
	MOV		ES,AX
	CALL kernel_lock_enter
	MOV EAX,0
	MOV   AX,CS
	PUSH  EAX
	CALL	inthandler20
	pop eax
	POP		EAX
	call signal_deal
	call kernel_lock_leave
	POPAD
	POP		DS
	POP		ES
  popa
  pop gs
  pop fs
  pop es
  pop ds
IRETD
asm_inthandler21:
  push ds
  push es
  push fs
  push gs
  pusha
	PUSH	ES
	PUSH	DS
	PUSHAD
	MOV		EAX,ESP
	PUSH	EAX
	MOV		AX,SS
	MOV		DS,AX
	MOV		ES,AX
	CALL kernel_lock_enter
	CALL	inthandler21
	POP		EAX
	call signal_deal
	call kernel_lock_leave
	POPAD
	POP		DS
	POP		ES
  popa
  pop gs
  pop fs
  pop es
  pop ds
	IRETD
asm_inthandler2c:
	push ds
  push es
  push fs
  push gs
  pusha
  mov ax, ss
  mov ds, ax
  mov es, ax
  call kernel_lock_enter
  CALL	inthandler2c
  call kernel_lock_leave
  popa
  pop gs
  pop fs
  pop es
  pop ds
  IRETD

x86_reschedule_entry:
  push ds
  push es
  push fs
  push gs
  pusha
  mov ax, ss
  mov ds, ax
  mov es, ax
  call kernel_lock_enter
  call scheduler_reschedule_interrupt
  call kernel_lock_leave
  popa
  pop gs
  pop fs
  pop es
  pop ds
  iretd
