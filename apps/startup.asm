; startup.asm -- Entry point for C .COM apps
; Linked first in the .COM binary, far-calls _main.
; _main returns via retf back to exec_reentry.

_TEXT   SEGMENT WORD PUBLIC 'CODE'
        ASSUME  CS:_TEXT

        PUBLIC  _start_
        EXTRN   _main_:far

_start_:
        call    far ptr _main_
        retf

_TEXT   ENDS
        END     _start_