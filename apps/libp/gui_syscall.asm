[BITS 32]
GLOBAL create_window,window_get_event,close_window,draw_px,window_refresh,window_get_fb,window_start_recv_keyboard,window_stop_recv_keyboard
GLOBAL window_get_key_press_data,window_get_key_press_status,window_get_key_up_data,window_get_key_up_status
[SECTION .text]
create_window:
    push ebx
    push ecx
    push edx
    push edi
    push esi
    mov eax,0x01
    mov ebx,[esp + 4 + 20] ; title
    mov ecx,[esp + 8 + 20] ; x
    mov edx,[esp + 12 + 20] ; y
    mov edi,[esp + 16 + 20] ; w
    mov esi,[esp + 20 + 20] ; h
    int 0x72
    pop esi
    pop edi
    pop edx
    pop ecx
    pop ebx
    ret
window_get_event:
    push ebx
    push ecx
    mov eax,0x02
    mov ebx,0
    mov ecx,[esp + 4 + 8]
    int 0x72
    pop ecx
    pop ebx
    ret
close_window:
    push ebx
    push eax
    push ecx
    mov eax,0x03
    mov ebx,0
    mov ecx,[esp + 4 + 12]
    int 0x72
    pop ecx
    pop eax
    pop ebx
    ret
draw_px:
    push eax
    push ecx
    push ebx
    push edx
    push esi
    push edi
    mov eax,0x04
    mov ebx,0
    mov ecx,[esp + 4 + 24] ; wnd
    mov esi,[esp + 8 + 24] ; x
    mov edx,[esp + 12 + 24] ; y
    mov edi,[esp + 16 + 24] ; color
    int 0x72
    pop edi
    pop esi
    pop edx
    pop ebx
    pop ecx
    pop eax
    ret
window_refresh:
    push eax
    push ebx
    push esi
    push edi
    push ecx
    push edx
    mov eax,0x05
    mov ebx, 0
    mov ecx,[esp + 4 + 24] ; wnd
    mov edx,[esp + 8 + 24] ; x1y1
    mov esi,[esp + 12 + 24]; x2y2
    int 0x72
    pop edx
    pop ecx
    pop edi
    pop esi
    pop ebx
    pop eax
    ret
window_get_fb:
    push ebx
    push ecx
    mov eax,0x06
    mov ebx,0
    mov ecx,[esp + 4 + 8] ; wnd
    int 0x72
    pop ecx
    pop ebx
    ret

window_start_recv_keyboard:
    push eax
    push ebx
    push ecx
    mov eax,0x07
    mov ebx,0
    mov ecx,[esp+4+12] ; wnd
    int 0x72
    pop ecx
    pop ebx
    pop eax
    ret

window_stop_recv_keyboard:
    push eax
    push ebx
    push ecx
    mov eax,0x08
    mov ebx,0
    mov ecx,[esp+4+12] ; wnd
    int 0x72
    pop ecx
    pop ebx
    pop eax
    ret
window_get_key_press_status:
    push ebx
    push ecx
    mov eax,0x09
    mov ebx,0
    mov ecx,[esp+4+8]
    int 0x72
    pop ecx
    pop ebx
    ret
window_get_key_press_data:
    push ebx
    push ecx
    mov eax,0x0a
    mov ebx,0
    mov ecx,[esp+4+8]
    int 0x72
    pop ecx
    pop ebx
    ret
window_get_key_up_status:
    push ebx
    push ecx
    mov eax,0x0b
    mov ebx,0
    mov ecx,[esp+4+8]
    int 0x72
    pop ecx
    pop ebx
    ret
window_get_key_up_data:
    push ebx
    push ecx
    mov eax,0x0c
    mov ebx,0
    mov ecx,[esp+4+8]
    int 0x72
    pop ecx
    pop ebx
    ret