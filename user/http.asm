; Minimal HTTP/1.0 client. A local host server listens on 127.0.0.1:8080.
BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    mov eax, SYS_SOCKET
    mov ebx, SOCK_TCP
    int 0x80
    test eax, eax
    js fail
    mov edi, eax
    mov eax, SYS_CONNECT
    mov ebx, edi
    mov ecx, 0x0a000202
    mov edx, 8080
    int 0x80
    cmp eax, -115
    jne fail
    mov eax, SYS_TICKS
    int 0x80
    add eax, 1000
    mov ebp, eax
connecting:
    mov eax, SYS_SOCK_STATUS
    mov ebx, edi
    int 0x80
    test eax, eax
    js fail
    test eax, SOCK_CONNECTED
    jnz send_request
    call pause
    jmp connecting
send_request:
    mov eax, SYS_SEND
    mov ebx, edi
    mov ecx, request
    mov edx, request_len
    int 0x80
    cmp eax, request_len
    jne fail
read_response:
    mov eax, SYS_RECV
    mov ebx, edi
    mov ecx, buffer
    mov edx, 256
    int 0x80
    cmp eax, -11
    je again
    test eax, eax
    js fail
    jz done
    add [received], eax
    mov [chunk], eax
    mov ecx, eax
    mov esi, buffer
    mov edx, [digest]
hash_chunk:
    movzx ebx, byte [esi]
    xor edx, ebx
    imul edx, edx, 0x01000193
    inc esi
    loop hash_chunk
    mov [digest], edx
    mov ecx, [chunk]
    mov eax, SYS_PRINT
    mov ebx, buffer
    int 0x80
    jmp read_response
again:
    call pause
    jmp read_response
done:
    mov eax, SYS_SOCK_CLOSE
    mov ebx, edi
    int 0x80
    test eax, eax
    jnz fail
    mov eax, [received]
    mov edi, hexout
    call hex32
    mov byte [hexout+8], ' '
    mov eax, [digest]
    mov edi, hexout+9
    call hex32
    mov byte [hexout+17], 10
    print stats, stats_len
    print hexout, 18
    print success, success_len
    exit 0
hex32:
    mov ecx, 8
hex_digit:
    rol eax, 4
    mov ebx, eax
    and ebx, 15
    mov dl, [digits+ebx]
    mov [edi], dl
    inc edi
    loop hex_digit
    ret
pause:
    mov eax, SYS_TICKS
    int 0x80
    sub eax, ebp
    jns fail
    mov eax, SYS_SLEEP
    mov ebx, 1
    int 0x80
    ret
fail:
    print failure, failure_len
    exit 1
section .rodata
request: db "GET /welcome.txt HTTP/1.0",13,10,"Host: localhost",13,10,"Connection: close",13,10,13,10
request_len equ $-request
success: db 10,"[user] HTTP download complete",10
success_len equ $-success
failure: db "[user] HTTP failed (check dhcp and host server on port 8080)",10
failure_len equ $-failure
digits: db "0123456789abcdef"
stats: db "HTTP bytes/hash: "
stats_len equ $-stats
section .data
digest: dd 0x811c9dc5
section .bss
received: resd 1
chunk: resd 1
hexout: resb 18
buffer: resb 256
