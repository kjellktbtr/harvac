; syscall.asm -- INT 0x40 handler and pctx stack for Harvac
;
; Parent context stack (pctx):
;   exec_save_parent_ctx_(child_seg) -- push current save area onto pctx stack
;   child_return_                    -- retf trampoline; falls through below
;   return_to_parent_                -- pop pctx, iret to parent; fallback restarts shell
;
; Segment allocator:
;   g_next_seg  -- next free paragraph (CS-relative); read/write via get_next_seg_/set_next_seg_

        .8086

        EXTRN   syscall_handler_c_:near
        EXTRN   timer_irq_handler_:near
        EXTRN   keyboard_irq_handler_:near
        EXTRN   exec_reentry_:near

_TEXT   SEGMENT WORD PUBLIC 'CODE'
        ASSUME  CS:_TEXT

KERNEL_SEG        EQU 02000h
KERNEL_STACK_TOP  EQU 0E000h

PCTX_MAX          EQU 8
PCTX_ENTRY_SZ     EQU 32   ; 16 fields * 2 bytes each (30 bytes used, 2 bytes pad)

; pctx entry layout (byte offsets):
;  0: child_seg   2: user_ss   4: user_sp   6: ax
;  8: bx         10: cx       12: dx       14: si
; 16: di         18: user_ds  20: user_es  22: user_bp
; 24: ip         26: cs       28: flags
; 30: (padding)

        PUBLIC  _int40_entry_

; --- INT 0x40 handler ---
_int40_entry_:
        ; Save user SS:SP at entry (IP/CS/FLAGS from INT are on top)
        mov     word ptr cs:[save_user_ss], ss
        mov     word ptr cs:[save_user_sp], sp

        ; Pop INT frame from user stack, save in kernel memory
        pop     word ptr cs:[save_ip]
        pop     word ptr cs:[save_cs]
        pop     word ptr cs:[save_flags]
        mov     word ptr cs:[save_user_sp], sp

        ; Save caller registers
        mov     word ptr cs:[save_ax], ax
        mov     word ptr cs:[save_bx], bx
        mov     word ptr cs:[save_cx], cx
        mov     word ptr cs:[save_dx], dx
        mov     word ptr cs:[save_si], si
        mov     word ptr cs:[save_di], di
        mov     word ptr cs:[save_user_ds], ds
        mov     word ptr cs:[save_user_es], es
        mov     word ptr cs:[save_user_bp], bp

        ; Switch to kernel stack
        cli
        mov     ax, KERNEL_SEG
        mov     ss, ax
        mov     sp, KERNEL_STACK_TOP
        sti
        mov     ds, ax
        mov     es, ax

        ; Set up C handler call
        ; __watcall: 1st=AX 2nd=DX 3rd=BX 4th=CX
        ; syscall_handler_c(ax, bx, cx, dx, si, di, caller_ds)
        ; Map: AX=origAX, DX=origBX, BX=origCX, CX=origDX
        mov     ax, word ptr cs:[save_ax]
        mov     dx, word ptr cs:[save_bx]
        mov     bx, word ptr cs:[save_cx]
        mov     cx, word ptr cs:[save_dx]

        push    word ptr cs:[save_user_ds]  ; 7th: caller_ds
        push    word ptr cs:[save_di]       ; 6th: DI
        push    word ptr cs:[save_si]       ; 5th: SI

        call    syscall_handler_c_
        ; retw $6 cleans up the 3 stack params

        ; Stash return value
        mov     word ptr cs:[save_ax], ax

        ; Switch back to user stack
        cli
        mov     ax, word ptr cs:[save_user_ss]
        mov     ss, ax
        mov     sp, word ptr cs:[save_user_sp]

        ; Push iret frame onto user stack
        push    word ptr cs:[save_flags]
        push    word ptr cs:[save_cs]
        push    word ptr cs:[save_ip]

        ; Restore caller registers
        mov     bp, word ptr cs:[save_user_bp]
        mov     ax, word ptr cs:[save_user_ds]
        mov     ds, ax
        mov     ax, word ptr cs:[save_user_es]
        mov     es, ax
        mov     di, word ptr cs:[save_di]
        mov     si, word ptr cs:[save_si]
        mov     dx, word ptr cs:[save_dx]
        mov     cx, word ptr cs:[save_cx]
        mov     bx, word ptr cs:[save_bx]
        mov     ax, word ptr cs:[save_ax]   ; return value

        sti
        iret

; --- IRQ0 handler (PIT timer) ---
        PUBLIC  irq0_stub_

irq0_stub_:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es
        mov     ax, KERNEL_SEG
        mov     ds, ax
        mov     es, ax
        call    timer_irq_handler_
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret

; --- IRQ1 handler (keyboard) ---
        PUBLIC  irq1_stub_

irq1_stub_:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es
        mov     ax, KERNEL_SEG
        mov     ds, ax
        mov     es, ax
        call    keyboard_irq_handler_
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret

; --- get_next_seg_ / set_next_seg_: C-callable accessors for g_next_seg ---
        PUBLIC  get_next_seg_
get_next_seg_   proc near
        mov     ax, word ptr cs:[g_next_seg]
        ret
get_next_seg_   endp

        PUBLIC  set_next_seg_
set_next_seg_   proc near
        ; watcall: AX = first param (new value)
        mov     word ptr cs:[g_next_seg], ax
        ret
set_next_seg_   endp

; --- exec_save_parent_ctx_: push save area + child_seg onto pctx stack ---
; Call from C before exec_far_jump when launching a child.
; Signature: void exec_save_parent_ctx(uint16_t child_seg)  -- AX = child_seg
        PUBLIC  exec_save_parent_ctx_

exec_save_parent_ctx_   proc near
        ; AX = child_seg
        push    bx
        push    cx
        push    dx
        push    si

        ; Check for stack overflow
        mov     bx, word ptr cs:[pctx_depth]
        cmp     bx, PCTX_MAX
        jae     save_ctx_done

        ; Compute byte offset of entry: pctx_depth * PCTX_ENTRY_SZ (32)
        ; (shift-by-CL: shl reg,imm is a 186+ encoding; CX is saved above)
        mov     dx, ax                              ; DX = child_seg (save AX)
        mov     ax, bx
        mov     cl, 5
        shl     ax, cl                              ; AX = depth * 32
        add     ax, offset pctx_stack               ; AX = CS: offset of entry
        mov     si, ax                              ; SI = entry offset

        ; Store child_seg in entry[0]
        mov     word ptr cs:[si + 0], dx

        ; Copy save area fields into entry
        mov     ax, word ptr cs:[save_user_ss]
        mov     word ptr cs:[si + 2], ax
        mov     ax, word ptr cs:[save_user_sp]
        mov     word ptr cs:[si + 4], ax
        mov     ax, word ptr cs:[save_ax]
        mov     word ptr cs:[si + 6], ax
        mov     ax, word ptr cs:[save_bx]
        mov     word ptr cs:[si + 8], ax
        mov     ax, word ptr cs:[save_cx]
        mov     word ptr cs:[si + 10], ax
        mov     ax, word ptr cs:[save_dx]
        mov     word ptr cs:[si + 12], ax
        mov     ax, word ptr cs:[save_si]
        mov     word ptr cs:[si + 14], ax
        mov     ax, word ptr cs:[save_di]
        mov     word ptr cs:[si + 16], ax
        mov     ax, word ptr cs:[save_user_ds]
        mov     word ptr cs:[si + 18], ax
        mov     ax, word ptr cs:[save_user_es]
        mov     word ptr cs:[si + 20], ax
        mov     ax, word ptr cs:[save_user_bp]
        mov     word ptr cs:[si + 22], ax
        mov     ax, word ptr cs:[save_ip]
        mov     word ptr cs:[si + 24], ax
        mov     ax, word ptr cs:[save_cs]
        mov     word ptr cs:[si + 26], ax
        mov     ax, word ptr cs:[save_flags]
        mov     word ptr cs:[si + 28], ax

        ; Increment depth
        inc     word ptr cs:[pctx_depth]

save_ctx_done:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
exec_save_parent_ctx_   endp

; --- child_return_: far retf trampoline ---
; Pushed as the return address onto the child's stack by exec_far_jump_sp.
; When child's _main does retf, CS:IP = KERNEL_SEG:child_return_.
; Falls through into return_to_parent_.
        PUBLIC  child_return_

child_return_   proc near
child_return_   endp

; --- return_to_parent_: pop pctx stack and iret to parent ---
; Callable from C (SYSCALL_EXIT) or fallen through from child_return_.
; On entry: CS = KERNEL_SEG; DS may be caller's segment.
; Never returns.
        PUBLIC  return_to_parent_

return_to_parent_   proc near
        cli
        ; Check depth without touching DS (use CS: prefix throughout)
        cmp     word ptr cs:[pctx_depth], 0
        je      rtp_fallback

        ; Pop top entry
        dec     word ptr cs:[pctx_depth]

        ; Compute entry offset: new pctx_depth * 32 + pctx_stack
        ; (CX is restored from the pctx entry below, safe to clobber)
        mov     ax, word ptr cs:[pctx_depth]
        mov     cl, 5
        shl     ax, cl                              ; AX = depth * 32
        add     ax, offset pctx_stack
        mov     si, ax                              ; SI = CS: offset of pctx entry

        ; Restore g_next_seg to child_seg (frees child's allocation)
        mov     ax, word ptr cs:[si + 0]
        mov     word ptr cs:[g_next_seg], ax

        ; Copy pctx entry into save_ area (reuses existing epilogue logic below)
        mov     ax, word ptr cs:[si + 2]
        mov     word ptr cs:[save_user_ss], ax
        mov     ax, word ptr cs:[si + 4]
        mov     word ptr cs:[save_user_sp], ax
        mov     ax, word ptr cs:[si + 6]
        mov     word ptr cs:[save_ax], ax
        mov     ax, word ptr cs:[si + 8]
        mov     word ptr cs:[save_bx], ax
        mov     ax, word ptr cs:[si + 10]
        mov     word ptr cs:[save_cx], ax
        mov     ax, word ptr cs:[si + 12]
        mov     word ptr cs:[save_dx], ax
        mov     ax, word ptr cs:[si + 14]
        mov     word ptr cs:[save_si], ax
        mov     ax, word ptr cs:[si + 16]
        mov     word ptr cs:[save_di], ax
        mov     ax, word ptr cs:[si + 18]
        mov     word ptr cs:[save_user_ds], ax
        mov     ax, word ptr cs:[si + 20]
        mov     word ptr cs:[save_user_es], ax
        mov     ax, word ptr cs:[si + 22]
        mov     word ptr cs:[save_user_bp], ax
        mov     ax, word ptr cs:[si + 24]
        mov     word ptr cs:[save_ip], ax
        mov     ax, word ptr cs:[si + 26]
        mov     word ptr cs:[save_cs], ax
        mov     ax, word ptr cs:[si + 28]
        mov     word ptr cs:[save_flags], ax

        ; Restore parent (mirrors _int40_entry_ epilogue)
        cli
        mov     ax, word ptr cs:[save_user_ss]
        mov     ss, ax
        mov     sp, word ptr cs:[save_user_sp]
        push    word ptr cs:[save_flags]
        push    word ptr cs:[save_cs]
        push    word ptr cs:[save_ip]
        mov     bp, word ptr cs:[save_user_bp]
        mov     ax, word ptr cs:[save_user_es]
        mov     es, ax
        mov     ax, word ptr cs:[save_user_ds]
        mov     ds, ax
        mov     di, word ptr cs:[save_di]
        mov     si, word ptr cs:[save_si]
        mov     dx, word ptr cs:[save_dx]
        mov     cx, word ptr cs:[save_cx]
        mov     bx, word ptr cs:[save_bx]
        mov     ax, 0   ; EXEC return value = 0 (child ran OK)
        sti
        iret

rtp_fallback:
        ; No parent: root shell called exit, restart via boot_shell
        mov     ax, KERNEL_SEG
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     sp, KERNEL_STACK_TOP
        sti
        jmp     exec_reentry_
return_to_parent_   endp

; --- Kernel save area (in code segment, accessed via cs:) ---
        align   2
save_user_ss    DW 0
save_user_sp    DW 0
save_ax         DW 0
save_bx         DW 0
save_cx         DW 0
save_dx         DW 0
save_si         DW 0
save_di         DW 0
save_user_ds    DW 0
save_user_es    DW 0
save_user_bp    DW 0
save_ip         DW 0
save_cs         DW 0
save_flags      DW 0

; --- Segment allocator ---
g_next_seg      DW 0        ; initialized by exec.c via set_next_seg after shell load

; --- Parent context stack ---
pctx_depth      DW 0
pctx_stack      DB 256 DUP (0)   ; PCTX_MAX(8) * PCTX_ENTRY_SZ(32) bytes

_TEXT   ENDS

        END
