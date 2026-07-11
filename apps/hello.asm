; hello.asm -- Simple .COM test for Harvac shell
; Print "Hello from .COM!" via INT 40h SYSCALL_WRITE_STDOUT,
; then return to shell via retf.
;
; NOTE: this is loaded at segment:0x0100 with ORG 0 in file.
; We adjust SI by adding 0x0100 to reach the correct data offset.

                .model small
                .code

                public _main
_main:
                push cs
                pop  ds              ; DS = CS for data access

                xor  ax, ax          ; clear AX (AH=0 = SYSCALL_WRITE_STDOUT)
                mov  si, offset msg
                add  si, 0100h          ; adjust for load at 0x0100
                mov  ah, 0              ; SYSCALL_WRITE_STDOUT
                int  40h
                mov  si, offset nl
                add  si, 0100h
                mov  ah, 0
                int  40h
                retf

msg             db   "Hello from .COM!", 0
nl              db   0Dh, 0Ah, 0

                end _main