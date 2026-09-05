bits 64
default rel
section .text
%define SYSCALL_SIGNAL_RETURN 0x65

; C uses SysV AMD64. Plant syscalls use RAX, RDI, RSI, RDX, R10, R8, R9.
; The saved argument slots make each wrapper's API-specific ordering explicit.
%define a0 qword [rsp]
%define a1 qword [rsp+8]
%define a2 qword [rsp+16]
%define a3 qword [rsp+24]
%define a4 qword [rsp+32]
%define a5 qword [rsp+40]
%macro CALL 2-9 0,0,0,0,0,0,rax
global %1
%1:
  push r9
  push r8
  push rcx
  push rdx
  push rsi
  push rdi
  mov rax, %2
  mov rdi, %3
  mov rsi, %4
  mov rdx, %5
  mov r10, %6
  mov r8, %7
  mov r9, %8
  syscall
  mov rax, %9
  add rsp, 48
  ret
%endmacro

CALL libp_syscall3, a0, a1, a2, a3
CALL vfs_syscall, 0x1a, a0, a1
CALL socket_syscall, 0x5e, a0, a1
CALL putch, 2, 0, 0, a0
CALL putstr, 5, 0, 0, a0
CALL print, 5, 0, 0, a0
CALL getch, 0x16, 1, 0, 0, 0, 0, 0, rdx
CALL input_char_inSM, 0x16, 2, 0, 0, 0, 0, 0, rdx
CALL scan, 0x16, 3, a1, a0
CALL goto_xy, 4, 0, a1, a0
CALL sleep, 6, 0, 0, a0
CALL api_malloc, 8, 0, a0, 0, 0, 0, 0, rdx
CALL api_heapsize, 9, 0, 0, 0, 0, 0, 0, rdx
CALL system, 0x19, 0, 0, a0
CALL Text_Draw_Box, 0x0c, a0, a1, a2, a3, a4
CALL api_beep, 0x0d, a0, a1, a2
CALL RAND, 0x2d
CALL api_get_command_line, 0x1b, 0, a1, a0
CALL Get_System_Version, 1, 0, 0, 0, 0, 0, 0, rdx
CALL _kbhit, 0x1d
CALL SwitchTo320X200X256, 3, 2
CALL SwitchToText8025, 3, 1
CALL SwitchTo320X200X256_BIOS, 0x21, 2
CALL SwitchToText8025_BIOS, 0x21, 1
CALL Draw_Char, 3, 3, a0, a1, a2, a3
CALL PrintChineseChar, 3, 4, a0, a1, a3, a2
CALL Draw_Box, 3, 5, a0, a1, a2, a3, a4
CALL Draw_Px, 3, 6, a0, a1, a2
CALL Draw_Str, 3, 7, a0, a1, a2, a3
CALL PrintChineseStr, 3, 8, a0, a1, a3, a2
CALL get_mouse, 0x0f
CALL mouse_support, 0x10
CALL TaskForever, 0x22, 3
CALL SendMessage, 0x22, 4, a0, a1, a2
CALL GetMessage, 0x22, 5, a1, a0
CALL MessageLength, 0x22, 6, a0
CALL NowTaskID, 0x22, 7
CALL haveMsg, 0x22, 8
CALL GetMessageAll, 0x22, 9, 0, a0
CALL AddThread, 0x22, 0x0a, a0, a1, a2, a3
CALL TaskLock, 0x22, 0x0b
CALL TaskUnlock, 0x22, 0x0c
CALL SubThread, 0x22, 0x0d, a0
CALL _exit, 0x1e, a0
CALL timer_alloc, 0x24
CALL timer_settime, 0x24, 1, a0
CALL timer_out, 0x24, 2
CALL timer_free, 0x24, 3
CALL get_hour_hex, 0x26
CALL get_min_hex, 0x26, 1
CALL get_sec_hex, 0x26, 2
CALL get_day_of_month, 0x26, 3
CALL get_day_of_week, 0x26, 4
CALL get_mon_hex, 0x26, 5
CALL get_year, 0x26, 6
CALL set_mode, 0x20, 5, a0, a1
CALL VBEDraw_Px, 0x27, a0, a1, a2
CALL VBEGet_Px, 0x28, a1, a0
CALL VBEGetBuffer, 0x29, a0
CALL VBESetBuffer, 0x2a, a0, a1, a2, a3, a4
CALL roll, 0x2b, a0
CALL VBEDraw_Box, 0x2c, a0, a1, a2, a3, a4
CALL set_cons_color, 0x23, 2, a0
CALL get_cons_color, 0x23, 1
CALL clock, 0x2e
CALL monotonic_ns, 0x5f
CALL init_float, 0x2f
CALL start_keyboard_message, 0x30
CALL key_press_status, 0x31
CALL key_up_status, 0x32
CALL get_key_press, 0x33
CALL get_key_up, 0x34
CALL sbrk, 0x35, a0
CALL api_get_env, 0x36, a0, a1
CALL exec, 0x39, a0, a1
CALL clear, 0x3a
CALL mem_total, 0x3e
CALL mem_used, 0x3f
CALL tty_start_cur_moving, 0x42
CALL tty_stop_cur_moving, 0x43
CALL tty_get_xsize, 0x45
CALL tty_get_ysize, 0x46
CALL logk, 0x48, a0
CALL signal, 0x49, a0, a1
CALL fork, 0x4a
CALL waittid, 0x4b, a0
CALL mouse_enable, 0x4d
CALL mouse_dat_status, 0x4e
CALL mouse_dat_get, 0x4f
CALL api_yield, 0x50
CALL tty_alloc, 0x51, a0, a1, a2, a3
CALL tty_set, 0x52, a0, a1
CALL tty_free, 0x53, a0
CALL set_rt, 0x54, a0
CALL use_keyboard, 0x55
CALL shared_memory_map_to, 0x57, 1, a0, a1, a3, a2, a4
CALL shared_memory_unmap, 0x57, 2, 0, 0, a0, 0, a1
CALL task_set_level_higher, 0x58, a0
CALL task_set_level_normal, 0x59, a0
CALL module_load, 0x5a, a0
CALL module_unload, 0x5b, a0
CALL module_list, 0x5c, a0, a1
CALL api_task_snapshot, 0x60, a0, a1, a2
CALL cpu_count, 0x61
CALL cpu_current, 0x61, 0, 0, 0, 0, 0, 0, rdx
CALL tty_notify_input, 0x62, a0
CALL perf_control, 0x63, a0
CALL input_wait, 0x64, a0

global get_xy
get_xy:
  mov eax, 0x0e
  syscall
  mov eax, edx
  shl eax, 16
  mov ax, si
  ret

global return_to_app
return_to_app:
  mov rdi, rsp
  mov eax, SYSCALL_SIGNAL_RETURN
  syscall
  ud2

global setjmp, longjmp
setjmp:
  mov [rdi], rbx
  mov [rdi+8], rbp
  mov [rdi+16], r12
  mov [rdi+24], r13
  mov [rdi+32], r14
  mov [rdi+40], r15
  lea rax, [rsp+8]
  mov [rdi+48], rax
  mov rax, [rsp]
  mov [rdi+56], rax
  stmxcsr [rdi+64]
  xor eax, eax
  ret
longjmp:
  mov eax, esi
  test eax, eax
  jnz .value
  inc eax
.value:
  mov rbx, [rdi]
  mov rbp, [rdi+8]
  mov r12, [rdi+16]
  mov r13, [rdi+24]
  mov r14, [rdi+32]
  mov r15, [rdi+40]
  mov rsp, [rdi+48]
  ldmxcsr [rdi+64]
  jmp [rdi+56]

section .note.GNU-stack noalloc noexec nowrite progbits
