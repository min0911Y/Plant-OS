[BITS 32]
  	GLOBAL	io_hlt, io_cli, io_sti, io_stihlt
		GLOBAL	io_in8,  io_in16,  io_in32
		GLOBAL	io_out8, io_out16, io_out32
		GLOBAL	io_load_eflags, io_store_eflags
		GLOBAL	load_gdtr, load_idtr,loader_main
		EXTERN DOSLDR_MAIN
		GLOBAL	load_cr0, store_cr0,memtest_sub,null_inthandler
		GLOBAL ASM_call
[SECTION .text]
%define ADR_BOTPAK 							   0x100000
io_hlt:	; void io_hlt(void);
		HLT
		RET

io_cli:	; void io_cli(void);
		CLI
		RET

io_sti:	; void io_sti(void);
		STI
		RET

io_stihlt:	; void io_stihlt(void);
		STI
		HLT
		RET

io_in8:	; int io_in8(int port);
		MOV		EDX,[ESP+4]		; port
		MOV		EAX,0
		IN		AL,DX
		RET

io_in16:	; int io_in16(int port);
		MOV		EDX,[ESP+4]		; port
		MOV		EAX,0
		IN		AX,DX
		RET

io_in32:	; int io_in32(int port);
		MOV		EDX,[ESP+4]		; port
		IN		EAX,DX
		RET

io_out8:	; void io_out8(int port, int data);
		MOV		EDX,[ESP+4]		; port
		MOV		AL,[ESP+8]		; data
		OUT		DX,AL
		RET

io_out16:	; void io_out16(int port, int data);
		MOV		EDX,[ESP+4]		; port
		MOV		EAX,[ESP+8]		; data
		OUT		DX,AX
		RET

io_out32:	; void io_out32(int port, int data);
		MOV		EDX,[ESP+4]		; port
		MOV		EAX,[ESP+8]		; data
		OUT		DX,EAX
		RET

io_load_eflags:	; int io_load_eflags(void);
		PUSHFD		; PUSH EFLAGS
		POP		EAX
		RET

io_store_eflags:	; void io_store_eflags(int eflags);
		MOV		EAX,[ESP+4]
		PUSH	EAX
		POPFD		; POP EFLAGS
		RET

load_gdtr:		; void load_gdtr(int limit, int addr);
		MOV		AX,[ESP+4]		; limit
		MOV		[ESP+6],AX
		LGDT	[ESP+6]
		RET

load_idtr:		; void load_idtr(int limit, int addr);
		MOV		AX,[ESP+4]		; limit
		MOV		[ESP+6],AX
		LIDT	[ESP+6]
		RET

load_cr0:		; int load_cr0(void);
		MOV		EAX,CR0
		RET

store_cr0:		; void store_cr0(int cr0);
		MOV		EAX,[ESP+4]
		MOV		CR0,EAX
		RET
extern flint
global floppy_int
floppy_int:
		cli
		pushad
		mov	eax,esp
		push	eax
		call	flint
		pop	eax
		popad
		sti
    IRETD

memtest_sub:	; unsigned int memtest_sub(unsigned int start, unsigned int end)
		CLI
		PUSH	EDI						; （由于还要使用EBX, ESI, EDI）
		PUSH	ESI
		PUSH	EBX
		MOV		ESI,0xaa55aa55			; pat0 = 0xaa55aa55;
		MOV		EDI,0x55aa55aa			; pat1 = 0x55aa55aa;
		MOV		EAX,[ESP+12+4]			; i = start;
		MOV		dword[testsize],1024*1024*1024	; testsize = 1024*1024*1024;
mts_loop:
		MOV		EBX,EAX
		ADD		EBX,[testsize]		 	; p = i + testsize;
		SUB		EBX,4					; p -= 4;
		MOV		EDX,[EBX]				; old = *p;
		MOV		[EBX],ESI				; *p = pat0;
		XOR		DWORD [EBX],0xffffffff	; *p ^= 0xffffffff;
		CMP		EDI,[EBX]				; if (*p != pat1) goto fin;
		JNE		mts_fin
		XOR		DWORD [EBX],0xffffffff	; *p ^= 0xffffffff;
		CMP		ESI,[EBX]				; if (*p != pat0) goto fin;
		JNE		mts_fin
		MOV		[EBX],EDX				; *p = old;
		ADD		EAX,[testsize]			; i += testsize;
		CMP		EAX,[ESP+12+8]			; if (i <= end) goto mts_loop;
		
		JBE		mts_loop
		STI
		POP		EBX
		POP		ESI
		POP		EDI
		RET
mts_fin:
		CMP		dword[testsize],0x1000	; if (testsize == 0x1000) goto mts_nomore;
		JE		mts_nomore
		SHR		dword[testsize],2	; testsize /= 4;
		JMP		mts_loop
mts_nomore:
		STI
		MOV		[EBX],EDX				; *p = old;
		POP		EBX
		POP		ESI
		POP		EDI
		RET

ASM_call:  ;移动光标
ret
null_inthandler:
	iretd
memcpy:
	mov	eax,[esi]
	mov	[edi],eax
	add	esi,4
	add	edi,4
	dec	ecx
	jnz	memcpy
	ret

global _IN
_IN:
	mov ebx,[esp+4]
	mov edx,[esp+8]
	push ebx
	push edx
	jmp far [esp]
loader_main:
	mov esp,stack_top
	jmp DOSLDR_MAIN
[SECTION .data]
testsize:	dd	0
[SECTION .bss]
stack: resb 40*1024
stack_top:
