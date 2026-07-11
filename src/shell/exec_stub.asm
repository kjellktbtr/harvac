; exec_stub.asm -- Return stub for .COM execution
;
; _exec_reentry: Called after .COM does retf. Restores kernel SS:SP
; and calls boot_shell_() to re-enter the shell (either SHELL.COM from
; the filesystem or the built-in kernel shell as fallback).

                .model small
                .code

                extrn boot_shell_:near

                public exec_reentry_

; ─── Return stub: restore kernel stack, re-enter shell ───
exec_reentry_   proc near
                cli
                mov  ax, 2000h          ; KERNEL_SEGMENT
                mov  ds, ax             ; restore DS to kernel segment!
                mov  es, ax             ; restore ES too
                mov  ss, ax
                mov  sp, 0E000h         ; STACK_TOP_OFF from entry.asm
                sti

                call boot_shell_        ; never returns
                ; Safety halt (should never reach)
                cli
                hlt
                jmp  $-2
exec_reentry_   endp

                end