[BITS 32]				
GLOBAL libp_syscall3
GLOBAL tty_alloc,tty_free,tty_set,tty_notify_input
GLOBAL putch,putstr,getch,get_mouse,get_xy,goto_xy
GLOBAL SwitchTo320X200X256,SwitchToText8025,Draw_Char,sleep
GLOBAL PrintChineseChar,PrintChineseStr,Draw_Str,api_malloc
GLOBAL print,scan,system,api_get_env
GLOBAL Draw_Box,Draw_Px,Text_Draw_Box,mem_used,mem_total
GLOBAL input_char_inSM,api_beep,RAND,api_get_command_line,Get_System_Version,_kbhit
GLOBAL SwitchTo320X200X256_BIOS,SwitchToText8025_BIOS
GLOBAL TaskForever,SendMessage,GetMessage,MessageLength,NowTaskID,exec
GLOBAL _exit,key_press_status,key_up_status,get_key_press,get_key_up,sbrk
GLOBAL timer_alloc,timer_settime,timer_out,timer_free,clock,monotonic_ns,start_keyboard_message,clear
GLOBAL haveMsg,PhyMemGetByte,GetMessageAll,PhyMemSetByte,api_heapsize
GLOBAL get_hour_hex,get_min_hex,get_sec_hex,get_day_of_month,get_day_of_week,get_mon_hex,get_year,AddThread,init_float
GLOBAL TaskLock,TaskUnlock,SubThread,set_mode,VBEDraw_Px,VBEGet_Px,VBEGetBuffer,VBESetBuffer,roll,VBEDraw_Box
GLOBAL tty_start_cur_moving,tty_stop_cur_moving,logk
GLOBAL tty_get_xsize,tty_get_ysize,mouse_support,signal,fork,waittid,mouse_enable,mouse_dat_status,mouse_read,api_yield,return_to_app,set_rt,shared_memory_map_to,shared_memory_unmap,task_set_level_higher,task_set_level_normal,use_keyboard
GLOBAL module_load,module_unload,module_list
GLOBAL api_task_snapshot,cpu_count,cpu_current,perf_control,input_wait
[SECTION .text]
libp_syscall3:
  push ebx
  mov eax, [esp + 8]
  mov ebx, [esp + 12]
  mov ecx, [esp + 16]
  mov edx, [esp + 20]
  int 0x36
  pop ebx
  ret

return_to_app:
  popa
  pop gs
  pop fs
  pop es
  pop ds
  ret
putch:
push	edx
push	eax
mov edx,[ss:esp+12]
mov eax,0x02
int 36h
pop	eax
pop	edx
ret

putstr:
push	edx
push	eax
mov edx,[ss:esp+12]
mov eax,0x05
int 36h
pop	eax
pop	edx
ret

getch:
push	edx
push	ebx
mov eax,0x16
mov ebx,0x1
int 36h
mov eax,edx
pop	ebx
pop	edx
ret

get_mouse:
push	ecx
push	edx
push	esi
mov	eax,0x0f
int	36h
mov	al,cl	; x
mov	ah,dl	; y
shl	eax,16
mov	ax,si	; btn
pop	esi
pop	edx
pop	ecx
ret
mouse_support:
  mov eax,0x10
  int 0x36
  ret
get_xy:
push	edx
push	ecx
mov eax,0x0e
int 36h
mov ax,dx	; x
shl	eax,16
mov	ax,cx	; y
pop	ecx
pop	edx
ret

goto_xy:
push	edx
push	ecx
push	eax
mov	edx,[ss:esp+16]
mov	ecx,[ss:esp+20]
mov	eax,0x04
int	36h
pop	eax
pop	ecx
pop	edx
ret

SwitchTo320X200X256:

push	ebx
mov	eax,0x03
mov	ebx,0x02
int	36h
pop	ebx

ret

SwitchToText8025:

push	ebx
mov	eax,0x03
mov	ebx,0x01
int	36h
pop	ebx

ret

Draw_Char:
push	eax
push	ebx
push	ecx
push	edx
push	esi
push	edi
mov	eax,0x03
mov	ebx,0x03
mov	ecx,[ss:esp+28]
mov	edx,[ss:esp+32]
mov	esi,[ss:esp+36]
mov	edi,[ss:esp+40]
int	36h
pop	edi
pop	esi
pop	edx
pop	ecx
pop	ebx
pop	eax
ret

sleep:
push	eax
push	ebx
mov	eax,0x06
mov	edx,[ss:esp+12]
int	36h
pop	ebx
pop	eax
ret

PrintChineseChar:
push	eax
push	ebx
push	ecx
push	edx
push	edi
push	esi
mov	eax,0x03
mov	ebx,0x04
mov	ecx,[ss:esp+28]
mov	edx,[ss:esp+32]
mov	edi,[ss:esp+36]
mov	esi,[ss:esp+40]
int	36h
pop	esi
pop	edi
pop	edx
pop	ecx
pop	ebx
pop	eax
ret

Draw_Str:
push	eax
push	ebx
push	ecx
push	edx
push	esi
push	edi
mov	eax,0x03
mov	ebx,0x07
mov	ecx,[ss:esp+28]
mov	edx,[ss:esp+32]
mov	esi,[ss:esp+36]
mov	edi,[ss:esp+40]
int	36h
pop	edi
pop	esi
pop	edx
pop	ecx
pop	ebx
pop	eax
ret

PrintChineseStr:
push	eax
push	ebx
push	ecx
push	edx
push	esi
push	edi
mov	eax,0x03
mov	ebx,0x08
mov	ecx,[ss:esp+28]
mov	edx,[ss:esp+32]
mov	edi,[ss:esp+36]
mov	esi,[ss:esp+40]
int	36h
pop	edi
pop	esi
pop	edx
pop	ecx
pop	ebx
pop	eax
ret

print:
push	eax
push	ebx
mov	eax,0x05
mov	edx,[ss:esp+12]
int	36h
pop	ebx
pop	eax
ret

scan:
push	eax
push	ebx
push	ecx
push	edx
mov	eax,0x16
mov	ebx,0x03
mov	edx,[ss:esp+20]
mov	ecx,[ss:esp+24]
int	36h
pop	edx
pop	ecx
pop	ebx
pop	eax
ret

api_malloc:
push	ecx
push	edx
mov	eax,0x08
mov	ecx,[ss:esp+12]
int	36h
mov	eax,edx
pop	edx
pop	ecx
ret

api_heapsize:
push	edx
mov	eax,0x09
int	36h
mov eax,edx
pop	edx
ret
system:
push edx
mov eax,0x19
mov edx,[ss:esp+8]
int 36h
pop edx
ret

Draw_Box:
push	eax
push	ebx
push	ecx
push	edx
push	esi
push	edi
push	ebp
mov	eax,0x03
mov	ebx,0x05
mov	ecx,[ss:esp+32]
mov	edx,[ss:esp+36]
mov	esi,[ss:esp+40]
mov	edi,[ss:esp+44]
mov	ebp,[ss:esp+48]
int	36h
pop	ebp
pop	edi
pop	esi
pop	edx
pop	ecx
pop	ebx
pop	eax
ret

Draw_Px:
push	eax
push	ebx
push	ecx
push	edx
push	esi
mov	eax,0x03
mov	ebx,0x06
mov	ecx,[ss:esp+24]
mov	edx,[ss:esp+28]
mov	esi,[ss:esp+32]
int	36h
pop	esi
pop	edx
pop	ecx
pop	ebx
pop	eax
ret

Text_Draw_Box:
push	eax
push	ebx
push	ecx
push	edx
push	esi
push	edi
mov	eax,0x0c
mov	ebx,[ss:esp+28]
mov	ecx,[ss:esp+32]
mov	edx,[ss:esp+36]
mov	esi,[ss:esp+40]
mov	edi,[ss:esp+44]
int	36h
pop	edi
pop	esi
pop	edx
pop	ecx
pop	ebx
pop	eax
ret

input_char_inSM:
push	ebx
push	edx
mov	eax,0x16
mov	ebx,0x02
int	36h
mov	eax,edx
pop	edx
pop	ebx
ret

api_beep:
push	eax
push	ebx
push	ecx
push	edx
mov	eax,0x0d
mov	ebx,[ss:esp+20]
mov	ecx,[ss:esp+24]
mov	edx,[ss:esp+28]
int	36h
pop	edx
pop	ecx
pop	ebx
pop	eax
ret

RAND:
; push edx
; MOV AX, 32767   ;产生从1到AX之间的随机数
; MOV DX, 41H ;用端口41H，或者上面说的其他端口也行
; OUT DX, AX  ;有这句话后，我发现就可以产生从1到AX之间的随机数了
; IN AL, DX   ;产生的随机数AL
; bswap eax
; MOV AX, 32767   ;产生从1到AX之间的随机数
; MOV DX, 41H ;用端口41H，或者上面说的其他端口也行
; OUT DX, AX  ;有这句话后，我发现就可以产生从1到AX之间的随机数了
; IN AL, DX   ;产生的随机数AL
; pop edx
mov eax,0x2d
int 36h
ret

api_get_command_line:
    push ecx
    push edx
    mov eax,0x1b
    mov edx,[esp+4+8]
    mov ecx,[esp+8+8]
    int 36h
    pop edx
    pop ecx
    ret

Get_System_Version:
    push edx
    mov eax,0x01
    int 36h
    mov eax,edx
    pop edx
    ret
_kbhit:
    mov eax,0x1d
    int 36h
    ret

SwitchTo320X200X256_BIOS:

	push	ebx
	mov	eax,0x21
	mov	ebx,0x02
	int	36h
	pop	ebx

	ret

SwitchToText8025_BIOS:

	push	ebx
	mov	eax,0x21
	mov	ebx,0x01
	int	36h
	pop	ebx

	ret

TaskForever:
	push	eax
	push	ebx
	mov	eax,0x22
	mov	ebx,0x03
	int	36h
	pop	ebx
	pop	eax
	ret

SendMessage:
	push	eax
	push	ebx
	push	ecx
	push	edx
	push	esi
	mov	eax,0x22
	mov ebx,0x04
	mov ecx,[ss:esp+24]
	mov edx,[ss:esp+28]
	mov esi,[ss:esp+32]
	int 36h
	pop	esi
	pop	edx
	pop	ecx
	pop	ebx
	pop	eax
	ret

GetMessage:
	push	eax
	push	ebx
	push	ecx
	push	edx
	mov	eax,0x22
	mov ebx,0x05
	mov edx,[ss:esp+20]
	mov ecx,[ss:esp+24]
	int 36h
	pop	edx
	pop	ecx
	pop	ebx
	pop	eax
	ret
	
MessageLength:
	push	ebx
	push	ecx
	mov	eax,0x22
	mov ebx,0x06
	mov ecx,[ss:esp+12]
	int 36h
	pop	edx
	pop	ecx
	ret

NowTaskID:
	push	ebx
	mov	eax,0x22
	mov	ebx,0x07
	int	36h
	pop	ebx
	ret

_exit:
	mov	eax,0x1e
	mov ebx,[esp + 4]
	int 36h
	ret
	;jmp	$

timer_alloc:
	push	eax
	push	ebx
	mov	eax,0x24
	mov	ebx,0x00
	int	36h
	pop	ebx
	pop	eax
	ret
timer_settime:
	push	eax
	push	ebx
	push	ecx
	mov	eax,0x24
	mov	ebx,0x01
	mov	ecx,[ss:esp+4+12]
	int	36h
	pop	ecx
	pop	ebx
	pop	eax
	ret
timer_out:
	push	ebx
	mov	eax,0x24
	mov	ebx,0x02
	int	36h
	pop	ebx
	ret
timer_free:
	push	eax
	push	ebx
	mov	eax,0x24
	mov	ebx,0x03
	int	36h
	pop	ebx
	pop	eax
	ret
haveMsg:
	push ebx
	mov eax,0x22
	mov ebx,0x08
	int 36h
	pop ebx
	ret
PhyMemGetByte:
	push	ds     ; 4
	mov   ax,1*8 ; 系统的数据段（可以访问到所有的内存）
	mov	  ds,ax
	mov   eax,[esp+4+4]
	mov	  al,[eax]
	pop   ds
	ret
GetMessageAll:
	push	eax
	push	ebx
	push	ecx
	push	edx
	mov	eax,0x22
	mov ebx,0x09
	mov edx,[ss:esp+20]
	;mov ecx,[ss:esp+24]
	int 36h
	pop	edx
	pop	ecx
	pop	ebx
	pop	eax
	ret
PhyMemSetByte:
	push	ds     ; 4
	push  ebx    ; 8
	mov   ax,1*8 ; 系统的数据段（可以访问到所有的内存）
	mov	  ds,ax
	mov   eax,[esp+4+8]
	mov   ebx,[esp+4+12]
	mov	  [eax],bl
	pop   ebx
	pop   ds
	ret
get_hour_hex:
	push ebx
	mov eax,0x26
	mov ebx,0x00
	int 36h
	pop ebx
	ret
get_min_hex:
	push ebx
	mov eax,0x26
	mov ebx,0x01
	int 36h
	pop ebx
	ret
get_sec_hex:
	push ebx
	mov eax,0x26
	mov ebx,0x02
	int 36h
	pop ebx
	ret
get_day_of_month:
	push ebx
	mov eax,0x26
	mov ebx,0x03
	int 36h
	pop ebx
	ret
get_day_of_week:
	push ebx
	mov eax,0x26
	mov ebx,0x04
	int 36h
	pop ebx
	ret
get_mon_hex:
	push ebx
	mov eax,0x26
	mov ebx,0x05
	int 36h
	pop ebx
	ret
get_year:
	push ebx
	mov eax,0x26
	mov ebx,0x06
	int 36h
	pop ebx
	ret
global setjmp
; int setjmp(jmp_buf env);
setjmp:
    mov ecx, [esp + 4]  ; ecx = env
    mov edx, [esp + 0]  ; edx = ret addr
    mov [ecx + 0], edx
    mov [ecx + 4], ebx
    mov [ecx + 8], esp
    mov [ecx + 12], ebp
    mov [ecx + 16], esi
    mov [ecx + 20], edi
    mov [ecx + 24], eax ; eax = trigblock()'s ret val

    xor eax, eax    ; setjmp ret val = 0
    ret

global longjmp
; void longjmp(jmp_buf env, int val)
longjmp:

    mov edx, [esp + 4]  ; edx = env
    mov eax, [esp + 8]  ; eax = val
    mov ecx, [edx + 0]  ; ecx = setjmp()'s ret val 
    mov ebx, [edx + 4]
    mov esp, [edx + 8]
    mov ebp, [edx + 12]
    mov esi, [edx + 16]
    mov edi, [edx + 20]
    
    ; make sure longjmp's ret val not 0
    test eax, eax   ; if eax == 0:
    jnz .1          ;   eax += 1
    inc eax         ; else: goto lable 1
.1: ; let longjmp's ret addr as setjmp's ret addr
    mov [esp + 0], ecx ; ret addr = ecx = setjmp's next code
    ret
AddThread:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov eax, 0x22
    mov ebx, 0x0a
    mov ecx, [esp + 24]
    mov edx, [esp + 28]
    mov esi, [esp + 32]
    mov edi, [esp + 36]
    int 36h
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
TaskLock:
	push eax
	push ebx
	mov eax,0x22
	mov ebx,0x0b
	int 36h
	pop ebx
	pop eax
	ret
TaskUnlock:
	push eax
	push ebx
	mov eax,0x22
	mov ebx,0x0c
	int 36h
	pop ebx
	pop eax
	ret
SubThread:
	push eax ; 4
	push ebx ; 8
	push ecx ; 12
	mov eax,0x22
	mov ebx,0x0d
	mov ecx,[esp+4+12]
	int 36h
	pop ecx
	pop ebx
	pop eax
	ret


set_mode:
	push ebx ; 4
	push ecx ; 8
	push edx ; 12
	mov eax,0x20
	mov ebx,0x05
	mov ecx,[esp+4 + 12]
	mov edx,[esp+8 + 12]
	int 36h
	pop edx
	pop ecx
	pop ebx
	ret
VBEDraw_Px:
	push eax ; 4
	push ebx ; 8
	push ecx ; 12
	push edx ; 16
	mov eax,0x27
	mov ebx,[esp + 4 + 16]
	mov ecx,[esp + 8 + 16]
	mov edx,[esp + 12 + 16]
	int 36h
	pop edx
	pop ecx
	pop ebx
	pop eax
	ret
VBEGet_Px:
	push ebx
	push ecx
	mov eax,0x28
	mov ebx,[esp + 8 + 8] ; y
	mov ecx,[esp + 4 + 8] ; x
	int 36h
	pop ecx
	pop ebx
	ret
VBEGetBuffer:
	push eax
	push ebx
	mov eax,0x29
	mov ebx,[esp + 4 + 8]
	int 36h
	pop ebx
	pop eax
	ret
VBESetBuffer:
	push eax
	push ebx
	push ecx
	push edx
	push esi
	push edi
	mov eax,0x2a
	mov ebx,[esp + 4 + 24]
	mov ecx,[esp + 8 + 24]
	mov edx,[esp + 12 + 24]
	mov esi,[esp + 16 + 24]
	mov edi,[esp + 20 + 24]
	int 36h
	pop edi
	pop esi
	pop edx
	pop ecx
	pop ebx
	pop eax
	ret
roll:
	push eax
	push ebx
	mov eax,0x2b
	mov ebx,[esp + 4 + 8]
	int 36h
	pop ebx
	pop eax
	ret
VBEDraw_Box:
push eax
push ebx
push ecx
push edx
push esi
push edi
mov	eax,0x2c
mov ebx,[esp+4+24]
mov ecx,[esp+8+24]
mov edx,[esp+12+24]
mov esi,[esp+16+24]
mov edi,[esp+20+24]
int	36h
pop edi
pop esi
pop edx
pop ecx
pop ebx
pop eax
ret
global	io_out8	,io_in8
io_out8:	; void io_out8(int port, int data);
		MOV		EDX,[ESP+4]		; port
		MOV		AL,[ESP+8]		; data
		OUT		DX,AL
		RET
io_in8:	; int io_in8(int port);
		MOV		EDX,[ESP+4]		; port
		MOV		EAX,0
		IN		AL,DX
		RET
global set_cons_color,get_cons_color
set_cons_color:
	push eax
	push ebx
	push ecx
	mov eax,0x23
	mov ebx,0x02
	mov ecx,[esp+4+12]
	int 36h
	pop ecx
	pop ebx
	pop eax
	ret
get_cons_color:
	push ebx
	mov eax,0x23
	mov ebx,0x01
	int 36h
	pop ebx
	ret
clock:
	mov eax,0x2e
	int 36h
	ret
monotonic_ns:
	mov eax,0x5f
	int 36h
	ret
init_float:
	pushad
	mov eax,0x2f
	int 36h
	popad
	ret
start_keyboard_message:
	mov eax,0x30
	int 36h
	ret
key_press_status:
	mov eax,0x31
	int 36h
	ret
key_up_status:
	mov eax,0x32
	int 36h
	ret
get_key_press:
	mov eax,0x33
	int 36h
	ret
get_key_up:
	mov eax,0x34
	int 36h
	ret
sbrk:
	push ebx
	mov ebx,[esp+8]
	mov eax,0x35
	int 0x36
	pop ebx
	ret
api_get_env:
	push ebx
	push ecx
	mov eax, 0x36
	mov ebx, [esp + 4 + 8] ; name
	mov ecx, [esp + 8 + 8]
	int 0x36
	pop ecx
	pop ebx
	ret
exec:
	push ebx
	push ecx
	mov eax,0x39
	mov ebx,[esp + 4 + 8]
	mov ecx,[esp + 8 + 8]
	int 0x36
	pop ecx
	pop ebx
	ret
clear:
	push eax
	mov eax,0x3a
	int 0x36
	pop eax
	ret
use_keyboard:
	mov eax,0x55
	int 0x36
	ret
mem_used:
	mov eax,0x3f
	int 0x36
	ret
mem_total:
	mov eax,0x3e
	int 0x36
	ret
tty_start_cur_moving:
	push eax
	mov eax,0x42
	int 0x36
	pop eax
	ret
tty_stop_cur_moving:
	push eax
	mov eax,0x43
	int 0x36
	pop eax
	ret
tty_get_xsize:
  mov eax,0x45
	int 0x36
	ret
tty_get_ysize:
	mov eax,0x46
	int 0x36
	ret
logk:
	push eax
	push ebx
	mov eax,0x48
	mov ebx,[esp+4+8]
	int 0x36
	pop ebx
	pop eax
	ret
signal:
	push ebx
	push ecx
	mov eax,0x49
	mov ebx,[esp+4+8]
	mov ecx,[esp+8+8]
	int 0x36
	pop ecx
	pop ebx
	ret
fork:
	mov eax,0x4a
	int 0x36
	ret
waittid:
	push ebx
	mov eax,0x4b
	mov ebx,[esp + 4 + 4]
	int 0x36
	pop ebx
	ret
mouse_enable:
	mov eax,0x4d
	int 0x36
	ret
mouse_dat_status:
	mov eax,0x4e
	int 0x36
	ret
mouse_read:
	push ebx
	mov ebx,[esp+8]
	mov eax,0x4f
	int 0x36
	pop ebx
	ret
api_yield:
	mov eax,0x50
	int 0x36
	ret
tty_alloc:
	push ebx
	push ecx
	push edx
	push esi ; 16
	mov ebx, [esp+16+4]
	mov ecx, [esp+16+8]
	mov edx, [esp+16+12]
	mov esi, [esp+16+16]
	mov eax,0x51
	int 0x36
	pop esi
	pop edx
	pop ecx
	pop ebx
	ret
tty_set:
	push eax
	push ebx
	push ecx ; 12
	mov ebx, [esp+12+4]
	mov ecx, [esp+12+8]
	mov eax, 0x52
	int 0x36
	pop ecx
	pop ebx
	pop eax
	ret
tty_free:
	push eax
	push ebx
	mov ebx, [esp +8 +4]
	mov eax,0x53
	int 0x36
	pop ebx
	pop eax
	ret

tty_notify_input:
	push ebx
	mov ebx,[esp + 4 + 4]
	mov eax,0x62
	int 0x36
	pop ebx
	ret
set_rt:
	push eax
	push ebx
	mov ebx,[esp+8+4]
	mov eax,0x54
	int 0x36
	pop ebx
	pop eax
	ret
shared_memory_map_to:
    push ebx
    push esi
    push edi
    push ebp
    mov eax,0x57
    mov ebx,0x01
    mov ecx,[esp + 4 + 16]
    mov edx,[esp + 8 + 16]
    mov edi,[esp + 12 + 16]
    mov esi,[esp + 16 + 16]
    mov ebp,[esp + 20 + 16]
    int 0x36
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
shared_memory_unmap:
    push ebx
    push esi
    push ebp
    mov eax,0x57
    mov ebx,0x02
    mov esi,[esp + 4 + 12]
    mov ebp,[esp + 8 + 12]
    int 0x36
    pop ebp
    pop esi
    pop ebx
    ret
task_set_level_higher:
	push eax
	push ebx
	mov eax,0x58
	mov ebx,[esp + 4+ 8]
	int 0x36
	pop ebx
	pop eax
	ret
task_set_level_normal:
	push eax
	push ebx
	mov eax,0x59
	mov ebx,[esp + 4+ 8]
	int 0x36
	pop ebx
	pop eax
	ret
module_load:
	push ebx
	mov eax,0x5a
	mov ebx,[esp + 4 + 4]
	int 0x36
	pop ebx
	ret
module_unload:
	push ebx
	mov eax,0x5b
	mov ebx,[esp + 4 + 4]
	int 0x36
	pop ebx
	ret
module_list:
	push ebx
	push ecx
	mov eax,0x5c
	mov ebx,[esp + 4 + 8]
	mov ecx,[esp + 8 + 8]
	int 0x36
	pop ecx
	pop ebx
	ret

api_task_snapshot:
	push ebx
	push ecx
	push edx
	mov eax,0x60
	mov ebx,[esp + 4 + 12]
	mov ecx,[esp + 8 + 12]
	mov edx,[esp + 12 + 12]
	int 0x36
	pop edx
	pop ecx
	pop ebx
	ret

cpu_count:
	mov eax,0x61
	int 0x36
	ret

cpu_current:
	mov eax,0x61
	int 0x36
	mov eax,edx
	ret

perf_control:
	push ebx
	mov ebx,[esp + 8]
	mov eax,0x63
	int 0x36
	pop ebx
	ret

input_wait:
	push ebx
	mov ebx,[esp + 8]
	mov eax,0x64
	int 0x36
	pop ebx
	ret
