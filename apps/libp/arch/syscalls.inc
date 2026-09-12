; Architecture-neutral Plant syscall numbers and argument ordering.
CALL libp_syscall3, a0, a1, a2, a3
CALL vfs_syscall, 0x1a, a0, a1
CALL socket_syscall, 0x5e, a0, a1
CALL putch, 2, 0, 0, a0
CALL putstr, 5, 0, 0, a0
CALL print, 5, 0, 0, a0
CALL getch, 0x16, 1, 0, 0, 0, 0, 0, DATA_RESULT
CALL input_char_inSM, 0x16, 2, 0, 0, 0, 0, 0, DATA_RESULT
CALL scan, 0x16, 3, a1, a0
CALL goto_xy, 4, 0, a1, a0
CALL sleep, 6, 0, 0, a0
CALL api_malloc, 8, 0, a0, 0, 0, 0, 0, DATA_RESULT
CALL api_heapsize, 9, 0, 0, 0, 0, 0, 0, DATA_RESULT
CALL system, 0x19, 0, 0, a0
CALL Text_Draw_Box, 0x0c, a0, a1, a2, a3, a4
CALL api_beep, 0x0d, a0, a1, a2
CALL RAND, 0x2d
CALL api_get_command_line, 0x1b, 0, a1, a0
CALL Get_System_Version, 1, 0, 0, 0, 0, 0, 0, DATA_RESULT
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
CALL mouse_support, 0x10
CALL TaskForever, 0x22, 3
CALL SendMessage, 0x22, 4, a0, a1, a2
CALL GetMessage, 0x22, 5, a1, a0
CALL MessageLength, 0x22, 6, a0
CALL NowTaskID, 0x22, 7
CALL getpid, 0x22, 0x0e
CALL getppid, 0x22, 0x0f
CALL haveMsg, 0x22, 8
CALL GetMessageAll, 0x22, 9, 0, a0
CALL TaskLock, 0x22, 0x0b
CALL TaskUnlock, 0x22, 0x0c
CALL SubThread, 0x22, 0x0d, a0
CALL api_exit, 0x1e, a0
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
CALL environment_query, 0x36, a0, a1, a2, a3
CALL exec, 0x39, a0, a1
CALL clear, 0x3a
CALL mem_total, 0x3e
CALL mem_used, 0x3f
CALL tty_start_cur_moving, 0x42
CALL tty_stop_cur_moving, 0x43
CALL tty_get_xsize, 0x45
CALL tty_get_ysize, 0x46
CALL logk, 0x48, a0
CALL signal_call, 0x49, a0, a1, a2, a3
CALL api_fork, 0x4a
CALL waittid, 0x4b, a0
CALL mouse_enable, 0x4d
CALL mouse_dat_status, 0x4e
CALL mouse_read, 0x4f, a0
CALL api_yield, 0x50
; TTY allocation: RPC server tid, opcode, columns, rows.
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
CALL cpu_current, 0x61, 0, 0, 0, 0, 0, 0, DATA_RESULT
CALL tty_notify_input, 0x62, a0
CALL perf_control, 0x63, a0
CALL input_wait, 0x64, a0
CALL futex_syscall, 0x67, a0, a1, a2
CALL thread_syscall, 0x68, a0, a1, a2
CALL native_thread_pointer, 0x68, 2
CALL _Exit, 0x68, 7, a0
CALL tty_get_pointer, 0x69, a0
CALL power_off, 0x6a
