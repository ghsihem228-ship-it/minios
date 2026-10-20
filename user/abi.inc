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
