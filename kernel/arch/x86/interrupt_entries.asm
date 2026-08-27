[BITS 32]
section .data
GLOBAL asm_inthandler21, asm_inthandler20
EXTERN inthandler21, inthandler20, inthandler2c, signal_deal
EXTERN x86_syscall_dispatch, x86_custom_syscall_dispatch
GLOBAL x86_syscall_entry, x86_custom_syscall_entry
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
  call x86_syscall_dispatch
  add esp, 4
  call signal_deal
  popa
  pop gs
  pop fs
  pop es
  pop ds
  iretd

x86_custom_syscall_entry:
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
  call x86_custom_syscall_dispatch
  add esp, 4
  call signal_deal
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
	CALL	flint
	POP		EAX
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
	PUSH	ES
	PUSH	DS
	PUSHAD
	MOV		EAX,ESP
	PUSH	EAX
	MOV		AX,SS
	MOV		DS,AX
	MOV		ES,AX
	CALL	PCNET_IRQ
	POP		EAX
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

EXTERN RTL8139_IRQ
GLOBAL RTL8139_ASM_INTHANDLER
RTL8139_ASM_INTHANDLER:
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
	CALL	RTL8139_IRQ
	POP		EAX
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
	CALL	sb16_handler
	POP		EAX
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
	CALL	ide_irq
	POP		EAX
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
	call rtc_irq
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
	MOV EAX,0
	MOV   AX,CS
	PUSH  EAX
	CALL	inthandler20
	pop eax
	POP		EAX
	call signal_deal
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
	CALL	inthandler21
	POP		EAX
	call signal_deal
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
  CALL	inthandler2c
  popa
  pop gs
  pop fs
  pop es
  pop ds
  IRETD
