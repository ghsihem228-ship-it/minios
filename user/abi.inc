%define SYS_PRINT 1
%define SYS_EXIT 2
%define SYS_YIELD 3
%define SYS_GETPID 4
%define SYS_TICKS 5
%define SYS_SLEEP 6
%define SYS_OPEN 7
%define SYS_READ 8
%define SYS_CLOSE 9
%define SYS_WRITE 10
%define SYS_SEEK 11
%define SYS_UNLINK 12
%define SYS_SYNC 13
%define SYS_STAT 14
%define SYS_SOCKET 15
%define SYS_CONNECT 16
%define SYS_SEND 17
%define SYS_RECV 18
%define SYS_SOCK_CLOSE 19
%define SYS_SOCK_STATUS 20
%define SYS_BIND 21
%define SOCK_UDP 1
%define SOCK_TCP 2
%define SOCK_CONNECTED 1
%define SOCK_READABLE 2
%define SOCK_WRITABLE 4
%define SOCK_EOF 8
%define SOCK_CONNECTING 16

%macro print 2
    mov eax, SYS_PRINT
    mov ebx, %1
    mov ecx, %2
    int 0x80
%endmacro
%macro exit 1
    mov eax, SYS_EXIT
    mov ebx, %1
    int 0x80
    ud2
%endmacro
