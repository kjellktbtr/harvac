; entry.asm -- Kernel entry point for Harvac
; Loaded by VBR at 0x1000:0x0000
; Sets up segments/stack, calls kmain_ (C entry)

        .8086

_TEXT   SEGMENT WORD PUBLIC 'CODE'
        ASSUME  CS:_TEXT, DS:_DATA, ES:_DATA, SS:_DATA

        EXTRN   kmain_:near

        PUBLIC  __entry

__entry:
        ; Set up data segment
        mov     ax, KERNEL_SEG
        mov     ds, ax
        mov     es, ax

        ; Set up stack at top of available memory
        mov     ss, ax
        mov     sp, STACK_TOP_OFF

        ; Call C main
        call    kmain_

        ; Halt if kmain returns (should not happen)
halt_loop:
        cli
        hlt
        jmp     halt_loop

KERNEL_SEG    EQU 02000h
STACK_TOP_OFF EQU 0E000h

_TEXT   ENDS

_DATA   SEGMENT WORD PUBLIC 'DATA'
_DATA   ENDS

CONST   SEGMENT WORD PUBLIC 'DATA'
CONST   ENDS

_BSS    SEGMENT WORD PUBLIC 'BSS'
_BSS    ENDS

        END