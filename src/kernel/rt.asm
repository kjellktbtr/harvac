; rt.asm -- OpenWatcom 16-bit runtime helpers for freestanding kernel
;
; The small model (-ms) generates calls to __U4D for 32-bit unsigned
; division.  This file provides it since we do not link any C runtime.

_TEXT SEGMENT WORD PUBLIC 'CODE'
    ASSUME CS:_TEXT

    ; __U4D -- unsigned 32-bit division
    ;
    ; Calling convention (per OpenWatcom 16-bit):
    ;   Input   DX:AX = dividend   (DX = high word)
    ;           BX:CX = divisor    (BX = high word)
    ;   Output  DX:AX = quotient
    ;           BX:CX = remainder
    ;   Destroys  nothing (callee saves SI, DI, BP)
    ;
    ; Algorithm: standard shift-and-subtract for full 32/32 case,
    ; fast 32/16 DIV for the common case where BX == 0.

    PUBLIC __U4D
__U4D PROC FAR
    push    si
    push    di
    push    bp

    ; Fast path: divisor high word is zero → single 32/16 DIV.
    or      bx, bx             ; BX == 0 ?
    jnz     short full_32

    ; --- 32/16 division: DI:AX / CX → AX=quot, DX=rem ---
    mov     di, ax
    mov     ax, dx             ; AX = high word of dividend
    xor     dx, dx
    div     cx                 ; AX = high_quot, DX = rem_(hi)
    xchg    ax, di             ; DI = high_quot, AX = low dividend word
    div     cx                 ; AX = low_quot, DX = rem_(lo)

    mov     cx, dx             ; CX = remainder low word
    xor     bx, bx             ; BX = remainder high word (= 0)
    mov     dx, di             ; DX = quotient high word
    ; AX already holds quotient low word
    jmp     short done

    ; --- Full 32/32 shift-and-subtract ---
full_32:
    mov     si, ax             ; SI = dividend low word
    mov     di, dx             ; DI = dividend high word
                                ; BX:CX = divisor (unchanged)

    xor     dx, dx             ; DX:AX = remainder (start at 0)
    xor     ax, ax
    mov     bp, 32             ; bit counter

div_loop:
    ; Shift dividend left (DI:SI) → CF
    shl     si, 1
    rcl     di, 1

    ; Shift carry into remainder (DX:AX)
    rcl     ax, 1
    rcl     dx, 1

    ; Compare remainder (DX:AX) with divisor (BX:CX)
    cmp     dx, bx
    jb      short no_sub
    ja      short do_sub
    cmp     ax, cx
    jb      short no_sub

do_sub:
    sub     ax, cx
    sbb     dx, bx
    inc     si                 ; set quotient bit

no_sub:
    dec     bp
    jnz     short div_loop

    ; After 32 iterations:
    ;   DI = quotient high word, SI = quotient low word
    ;   DX = remainder high word, AX = remainder low word
    mov     bx, dx             ; BX = remainder high word
    mov     cx, ax             ; CX = remainder low word
    mov     ax, si             ; AX = quotient low word
    mov     dx, di             ; DX = quotient high word

done:
    pop     bp
    pop     di
    pop     si
    ret
__U4D ENDP

_TEXT ENDS
    END