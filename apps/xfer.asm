; SPDX-License-Identifier: GPL-2.0-only
; Copyright (C) 2026 Kjell Kristian Grane Torgersen
;
; xfer.asm -- Harvac serial file-transfer agent.
;
; Ported from /home/kjell/git/serial-xfer/xfercom.asm (DOS NASM original).
; Build: nasm -f bin apps/xfer.asm -o build/XFER.COM
;
; Wire protocol: COBS framing + per-packet CRC-16/CCITT + whole-file CRC-32 +
; stop-and-wait ACK.  Protocol version 1: T_ENTRY carries FAT time/date
; (zero on Harvac, which does not set timestamps yet); the v1 timestamp
; bytes appended to the upload CLOSE payload are accepted but ignored.
; Connect from the host with:
;   python host.py --socket /tmp/harvac-xfer.sock <command>
;
; OS differences from the DOS original:
;   - File ops: INT 40h (Harvac) instead of INT 21h (DOS).
;   - Directory listing: SYSCALL_OPENDIR/READDIR/CLOSEDIR instead of findfirst/findnext.
;   - Screen output: SYSCALL_WRITE_VGA (0x0A) -- VGA only, never serial (avoids
;     corrupting the binary protocol stream).
;   - Keyboard abort: direct PS/2 port poll instead of INT 16h.
;   - Exit: SYSCALL_EXIT (0x41) instead of INT 21h/4Ch.
;   - GET replies send CRC-only CLOSE (no timestamps); upload CLOSE accepts
;     and ignores the v1 timestamp bytes (Harvac FAT16 has no mtime yet).
;   - Paths in LIST/GET/PREAD/PWRITE: DOS handled full paths in INT 21h;
;     Harvac syscalls take a bare name in the CWD, so the agent CHDIRs to
;     the directory part (path_enter/path_leave) and operates on the
;     basename. PWRITE refuses to *create* outside the root, because
;     SYSCALL_CREATE is root-only and the file would land in the wrong dir.
;
; CRC, COBS, framing, UART and parse_args are verbatim from the DOS original.

cpu 8086
bits 16
org 0x100

; ---------------------------------------------------------------------------
; Harvac INT 0x40 syscall numbers
; ---------------------------------------------------------------------------
%define SYSCALL_WRITE_VGA   0x0A  ; SI=str offset; VGA screen only (not serial)
%define SYSCALL_KEY_AVAIL   0x0B  ; -> AX nonzero if key buffered (never serial)
%define SYSCALL_READ_CHAR   0x03  ; -> AH=scancode, AL=ascii (blocking)
%define SYSCALL_OPEN        0x10  ; AL=flags, BX=name_offset -> AX=handle/0xFFFF
%define SYSCALL_CLOSE       0x11  ; BX=handle
%define SYSCALL_READ        0x12  ; BX=handle, CX=buf_ptr, DX=count -> AX=bytes
%define SYSCALL_WRITE       0x13  ; BX=handle, CX=buf_ptr, DX=count -> AX=bytes
%define SYSCALL_SEEK        0x14  ; BX=handle, CX=pos_low, DX=pos_high
%define SYSCALL_CREATE      0x15  ; BX=name_offset -> AX=handle/0xFFFF
%define SYSCALL_DELETE      0x16  ; BX=name_offset -> AX=0/error
%define SYSCALL_RENAME      0x18  ; SI=old_name, DI=new_name -> AX=0/error
%define SYSCALL_OPENDIR     0x20  ; CX=dir_buf_offset -> AX=0/error
%define SYSCALL_READDIR     0x21  ; CX=dir_buf, DX=dirent_buf -> AX=0/err
%define SYSCALL_CLOSEDIR    0x22  ; CX=dir_buf
%define SYSCALL_MKDIR       0x23  ; BX=name_offset -> AX=0/error
%define SYSCALL_RMDIR       0x24  ; BX=name_offset -> AX=0/error
%define SYSCALL_CHDIR       0x25  ; BX=path_offset -> AX=0/error
%define SYSCALL_GETCWD      0x26  ; CX=buf_offset, DX=buflen
%define SYSCALL_EXIT        0x41  ; exit child, return to shell

; ---------------------------------------------------------------------------
; Protocol packet-type constants (unchanged from xfercom.asm)
; ---------------------------------------------------------------------------
%define CHUNK     128

%define T_OPEN    1
%define T_DATA    2
%define T_CLOSE   3
%define T_QUIT    4
%define T_GET     5
%define T_MKDIR   6
%define T_LIST    7
%define T_ENTRY   8
%define T_MSG     9
%define T_DEL     10
%define T_RMD     11
%define T_REN     12
%define T_PREAD   13
%define T_PWRITE  14
%define T_RAW     15
%define T_VERSION 16
%define T_ACK     0x10
%define T_NAK     0x11

; fat16_dirent_t field offsets
%define D_ATTRS   11
%define D_TIME    22
%define D_DATE    24
%define D_FSIZE   28

; ---------------------------------------------------------------------------
; Entry / main loop  (mirrors xfercom.asm start:)
; ---------------------------------------------------------------------------
start:
    call parse_args              ; sets v_base, v_div, v_com, v_baudstr
    call uart_init
    mov word [v_fd], -1
    mov word [v_wcrc], 0
    mov word [v_wcrc+2], 0
    ; Banner: "xfer ready on COMn at NNNN baud - press Q to quit\r\n"
    mov si, msg_rdyA             ; "xfer ready on COM"
    call putstr
    ; Write COM port digit (stored in a 2-byte buffer)
    mov al, '0'
    add al, [v_com]
    mov [v_cmbuf], al
    mov byte [v_cmbuf + 1], 0
    mov si, v_cmbuf
    call putstr
    mov si, msg_rdyB             ; " at "
    call putstr
    mov si, [v_baudstr]          ; baud rate digits
    call putstr
    mov si, msg_rdyC             ; " baud - press Q to quit"
    call puts                    ; adds CRLF

.main_loop:
    call read_frame              ; AX = decoded packet length in pk[]
    cmp ax, 4
    jl .main_loop
    mov [v_n], ax

    ; Extract CRC from packet tail
    mov si, pk
    add si, ax
    mov dh, [si-2]
    mov dl, [si-1]
    mov [v_got16], dx

    ; Compute expected CRC
    mov si, pk
    mov cx, [v_n]
    sub cx, 2
    call crc16                   ; AX = expected CRC

    ; Extract type/seq/dlen
    mov bl, [pk]
    mov [v_type], bl
    mov bl, [pk+1]
    mov [v_seq], bl
    mov cx, [v_n]
    sub cx, 4
    mov [v_dlen], cx

    ; CRC check
    cmp ax, [v_got16]
    je .crc_ok
    mov bl, T_NAK
    mov bh, [v_seq]
    mov si, pk
    xor cx, cx
    call send_packet
    jmp .main_loop

.crc_ok:
    mov al, [v_type]
    cmp al, T_OPEN
    je .h_open
    cmp al, T_DATA
    je .h_data
    cmp al, T_CLOSE
    je .h_close
    cmp al, T_GET
    je .h_get
    cmp al, T_LIST
    je .h_list
    cmp al, T_MKDIR
    je .h_mkdir
    cmp al, T_MSG
    je .h_msg
    cmp al, T_DEL
    je .h_del
    cmp al, T_RMD
    je .h_rmd
    cmp al, T_REN
    je .h_ren
    cmp al, T_PREAD
    je .h_pread
    cmp al, T_PWRITE
    je .h_pwrite
    cmp al, T_RAW
    je .h_raw
    cmp al, T_QUIT
    je .h_quit_ack
    cmp al, T_VERSION
    je .h_version
    ; default: empty ACK
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, pk
    xor cx, cx
    call send_packet
    jmp .main_loop

; --- OPEN: create/truncate file for writing ---
.h_open:
    mov bx, [v_dlen]
    mov si, pk
    add si, bx
    mov byte [si+2], 0           ; NUL-terminate filename
    cmp word [v_fd], 0
    jl .ho_noclose
    mov bx, [v_fd]
    call do_close
.ho_noclose:
    mov dx, pk+2
    mov bx, 0x0101               ; O_CREAT|O_WRONLY
    call do_open
    mov [v_fd], ax
    mov word [v_wcrc], 0
    mov word [v_wcrc+2], 0
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, pk
    xor cx, cx
    call send_packet
    jmp .main_loop

; --- DATA: write chunk to open file, update whole-file CRC ---
.h_data:
    cmp word [v_fd], 0
    jl .hd_nowrite
    mov bx, [v_fd]
    mov dx, pk+2
    mov cx, [v_dlen]
    call do_write
.hd_nowrite:
    mov si, pk+2
    mov cx, [v_dlen]
    mov ax, [v_wcrc]
    mov dx, [v_wcrc+2]
    call crc32                   ; DX:AX = updated whole-file CRC
    mov [v_wcrc], ax
    mov [v_wcrc+2], dx
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, pk
    xor cx, cx
    call send_packet
    jmp .main_loop

; --- CLOSE: verify CRC-32, close file, ACK with status byte ---
.h_close:
    mov byte [v_status], 1       ; assume bad
    mov cx, [v_dlen]
    cmp cx, 4
    jl .hc_setstatus
    mov al, [pk+2]
    cmp al, [v_wcrc+3]           ; expected (wcrc >> 24) & 0xFF
    jne .hc_setstatus
    mov al, [pk+3]
    cmp al, [v_wcrc+2]
    jne .hc_setstatus
    mov al, [pk+4]
    cmp al, [v_wcrc+1]
    jne .hc_setstatus
    mov al, [pk+5]
    cmp al, [v_wcrc]
    jne .hc_setstatus
    mov byte [v_status], 0       ; CRC match
.hc_setstatus:
    cmp word [v_fd], 0
    jl .hc_nofd
    mov bx, [v_fd]
    call do_close
    mov word [v_fd], -1
.hc_nofd:
    mov al, [v_status]
    mov [eb], al
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, eb
    mov cx, 1
    call send_packet
    jmp .main_loop

; --- GET: send a file to the host ---
.h_get:
    mov bx, [v_dlen]
    mov si, pk
    add si, bx
    mov byte [si+2], 0
    mov si, pk+2
    mov al, [v_seq]
    call serve_get
    jmp .main_loop

; --- LIST: enumerate current directory ---
.h_list:
    mov bx, [v_dlen]
    mov si, pk
    add si, bx
    mov byte [si+2], 0
    mov si, pk+2
    mov al, [v_seq]
    call serve_list
    jmp .main_loop

; --- MKDIR ---
.h_mkdir:
    mov bx, [v_dlen]
    mov si, pk
    add si, bx
    mov byte [si+2], 0
    mov dx, pk+2
    call do_mkdir
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, pk
    xor cx, cx
    call send_packet
    jmp .main_loop

; --- MSG: display message on screen (not on serial), then ACK ---
.h_msg:
    mov bx, [v_dlen]
    mov si, pk
    add si, bx
    mov byte [si+2], 0           ; NUL-terminate message text
    mov si, pk+2
    call puts                    ; WRITE_VGA + CRLF
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, pk
    xor cx, cx
    call send_packet
    jmp .main_loop

; --- RAW: print DATA verbatim on screen (no CRLF), then ACK ---
.h_raw:
    mov bx, [v_dlen]
    test bx, bx
    jz .hraw_ack
    mov si, pk
    add si, bx
    mov byte [si+2], 0           ; NUL-terminate
    mov si, pk+2
    call putstr                  ; WRITE_VGA, no CRLF
.hraw_ack:
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, pk
    xor cx, cx
    call send_packet
    jmp .main_loop

; --- VERSION: reply ACK with payload 0x01 = protocol v1 (T_ENTRY carries
; time/date; the extra timestamp bytes on upload CLOSE are ignored) ---
.h_version:
    mov byte [eb], 1
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, eb
    mov cx, 1
    call send_packet
    jmp .main_loop

; --- DEL ---
.h_del:
    mov bx, [v_dlen]
    mov si, pk
    add si, bx
    mov byte [si+2], 0
    mov dx, pk+2
    call do_delete
    jmp .v2_ack_ax

; --- RMD ---
.h_rmd:
    mov bx, [v_dlen]
    mov si, pk
    add si, bx
    mov byte [si+2], 0
    mov dx, pk+2
    call do_rmdir
    jmp .v2_ack_ax

; --- REN: DATA = old \0 new ---
.h_ren:
    mov bx, [v_dlen]
    mov si, pk
    add si, bx
    mov byte [si+2], 0           ; NUL-terminate 'new' string
    mov si, pk+2                 ; scan past 'old' to find 'new'
.hr_scan:
    lodsb
    test al, al
    jnz .hr_scan
    mov dx, pk+2                 ; old name
    mov di, si                   ; new name (SI is now past the NUL)
    call do_rename
    jmp .v2_ack_ax

; Shared v2 reply: AX=0 ok / nonzero err -> ACK with 1-byte status (0/1).
.v2_ack_ax:
    mov byte [eb], 0
    test ax, ax
    jz .v2_ack_send
    mov byte [eb], 1
.v2_ack_send:
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, eb
    mov cx, 1
    call send_packet
    jmp .main_loop

; --- PREAD: DATA = offset(4 LE) length(2 LE) name ---
.h_pread:
    mov bx, [v_dlen]
    mov si, pk
    add si, bx
    mov byte [si+2], 0
    mov dx, pk+8                 ; filename (may carry a directory part)
    call path_enter              ; DX -> basename, CHDIR to its directory
    test ax, ax
    jnz .hpr_empty               ; unknown directory: reply empty (= EOF)
    xor bx, bx                   ; O_RDONLY
    call do_open
    call path_leave              ; handle survives the CWD restore
    cmp ax, 0
    jl .hpr_empty
    mov [v_rfd], ax
    mov bx, ax
    mov dx, [pk+2]               ; offset low word
    mov cx, [pk+4]               ; offset high word
    call do_lseek
    mov cx, [pk+6]               ; byte count
    cmp cx, CHUNK
    jbe .hpr_rd
    mov cx, CHUNK
.hpr_rd:
    mov bx, [v_rfd]
    mov dx, fbuf
    call do_read
    cmp ax, 0
    jge .hpr_have
    xor ax, ax
.hpr_have:
    mov [v_got], ax
    mov bx, [v_rfd]
    call do_close
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, fbuf
    mov cx, [v_got]
    call send_packet
    jmp .main_loop
.hpr_empty:
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, fbuf
    xor cx, cx
    call send_packet
    jmp .main_loop

; --- PWRITE: DATA = offset(4 LE) name \0 bytes ---
.h_pwrite:
    mov si, pk+6                 ; scan past filename to the bytes
.hpw_scan:
    lodsb
    test al, al
    jnz .hpw_scan
    mov ax, pk+2
    add ax, [v_dlen]
    sub ax, si
    mov [v_got], ax
    mov [v_name], si             ; pointer to bytes
    mov dx, pk+6                 ; filename (may carry a directory part)
    call path_enter              ; DX -> basename, CHDIR to its directory
    test ax, ax
    jnz .hpw_err                 ; unknown directory
    push dx
    mov bx, 2                    ; O_RDWR
    call do_open
    pop dx
    cmp ax, 0
    jge .hpw_open
    ; Fall back to create -- but SYSCALL_CREATE only writes the root
    ; directory, so refuse unless the effective directory IS the root
    ; (the file would otherwise silently land in the wrong place).
    mov ah, SYSCALL_GETCWD
    xor al, al
    xor bx, bx
    mov cx, eb
    push dx
    mov dx, 32
    xor si, si
    xor di, di
    int 0x40
    pop dx
    cmp word [eb], 0x002F        ; "/" NUL -> effective dir is the root?
    jne .hpw_errleave
    mov bx, 0x0101               ; O_CREAT|O_WRONLY
    call do_open
.hpw_open:
    call path_leave              ; handle survives the CWD restore
    cmp ax, 0
    jl .hpw_err
    mov [v_rfd], ax
    mov bx, ax
    mov dx, [pk+2]
    mov cx, [pk+4]
    call do_lseek
    mov bx, [v_rfd]
    mov dx, [v_name]
    mov cx, [v_got]
    call do_write
    mov bx, [v_rfd]
    call do_close
    xor ax, ax
    jmp .v2_ack_ax
.hpw_errleave:
    call path_leave
.hpw_err:
    mov ax, -1
    jmp .v2_ack_ax

; --- QUIT ---
.h_quit_ack:
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, pk
    xor cx, cx
    call send_packet
do_quit:
    cmp word [v_fd], 0
    jl .dq_exit
    mov bx, [v_fd]
    call do_close
    mov word [v_fd], -1
.dq_exit:
    mov ah, SYSCALL_EXIT
    xor al, al
    int 0x40
    ; unreachable

; ===========================================================================
; Protocol helpers (verbatim from xfercom.asm -- pure computation, no OS calls)
; ===========================================================================

; crc16(SI=ptr, CX=n) -> AX=crc.  CCITT poly 0x1021, init 0xFFFF.
crc16:
    push bx
    push cx
    push si
    mov ax, 0xFFFF
    jcxz .done
.byte:
    mov bl, [si]
    inc si
    mov bh, bl
    xor bl, bl
    xor ax, bx
    push cx
    mov cx, 8
.bit:
    test ax, 0x8000
    jz .no
    shl ax, 1
    xor ax, 0x1021
    jmp .next
.no:
    shl ax, 1
.next:
    loop .bit
    pop cx
    loop .byte
.done:
    pop si
    pop cx
    pop bx
    ret

; crc32(SI=ptr, CX=n, DX:AX=crc) -> DX:AX=crc.  Reflected 0xEDB88320, zlib.
crc32:
    push si
    push cx
    push bx
    push di
    not ax
    not dx
    jcxz .done
.byte:
    mov bl, [si]
    inc si
    xor al, bl
    mov di, 8
.bit:
    test al, 1
    jz .shift
    shr dx, 1
    rcr ax, 1
    xor dx, 0xEDB8
    xor ax, 0x8320
    jmp .nextbit
.shift:
    shr dx, 1
    rcr ax, 1
.nextbit:
    dec di
    jnz .bit
    loop .byte
.done:
    not ax
    not dx
    pop di
    pop bx
    pop cx
    pop si
    ret

; cobs_decode(SI=in, CX=len, DI=out) -> AX=decoded length.
cobs_decode:
    push si
    push di
    push bx
    push cx
    push dx
    mov bx, si
    add bx, cx
    mov dx, di
.while:
    cmp si, bx
    jae .done
    mov al, [si]
    inc si
    mov ah, al
    mov cl, al
    xor ch, ch
    dec cx
    jcxz .after
.copy:
    cmp si, bx
    jae .after
    mov al, [si]
    inc si
    mov [di], al
    inc di
    loop .copy
.after:
    cmp ah, 0xFF
    je .while
    cmp si, bx
    jae .while
    mov byte [di], 0
    inc di
    jmp .while
.done:
    mov ax, di
    sub ax, dx
    pop dx
    pop cx
    pop bx
    pop di
    pop si
    ret

; cobs_encode(SI=in, CX=len, DI=out) -> AX=encoded length.
cobs_encode:
    push si
    push di
    push bx
    push bp
    push cx
    push dx
    mov bx, di
    lea bp, [di+1]
    mov dl, 1
    jcxz .flush
.loop:
    mov al, [si]
    inc si
    test al, al
    jnz .nonzero
    mov [bx], dl
    mov bx, bp
    inc bp
    mov dl, 1
    jmp .next
.nonzero:
    mov [bp], al
    inc bp
    inc dl
    cmp dl, 0xFF
    jne .next
    mov [bx], dl
    mov bx, bp
    inc bp
    mov dl, 1
.next:
    loop .loop
.flush:
    mov [bx], dl
    mov ax, bp
    sub ax, di
    pop dx
    pop cx
    pop bp
    pop bx
    pop di
    pop si
    ret

; read_frame() -> AX = decoded length (into pk[]).
read_frame:
    push si
    push di
    push cx
    push bx
    mov di, rxf
    xor cx, cx
.rf_read:
    call uart_getc
    test al, al
    jz .rf_eof
    cmp cx, 600
    jae .rf_read
    mov [di], al
    inc di
    inc cx
    jmp .rf_read
.rf_eof:
    mov si, rxf
    mov di, pk
    call cobs_decode
    pop bx
    pop cx
    pop di
    pop si
    ret

; send_packet(BL=type, BH=seq, SI=data, CX=dlen).
send_packet:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, op
    mov [di], bl
    mov [di+1], bh
    lea di, [op+2]
    jcxz .nocopy
.copy:
    mov al, [si]
    inc si
    mov [di], al
    inc di
    loop .copy
.nocopy:
    mov dx, di
    sub dx, op
    push dx
    mov si, op
    mov cx, dx
    call crc16
    pop dx
    mov di, op
    add di, dx
    mov [di], ah
    mov [di+1], al
    add dx, 2
    mov si, op
    mov cx, dx
    mov di, tx
    call cobs_encode
    mov cx, ax
    mov si, tx
    jcxz .send0
.send:
    mov al, [si]
    inc si
    call uart_putc
    loop .send
.send0:
    xor al, al
    call uart_putc
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wait_ack() -> AX=1 if next frame is ACK.
wait_ack:
    call read_frame
    cmp ax, 4
    jl .no
    mov al, [pk]
    cmp al, T_QUIT
    je do_quit
    cmp al, T_ACK
    jne .no
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

; crc_to_be(DX:AX = crc) -> packs big-endian into eb[0..3].
crc_to_be:
    mov [eb+0], dh
    mov [eb+1], dl
    mov [eb+2], ah
    mov [eb+3], al
    ret

; ===========================================================================
; File-transfer helpers
; ===========================================================================

; serve_get(SI=name, AL=seq) -- stream file to host (protocol v0: no timestamps).
serve_get:
    mov [v_seq], al
    mov dx, si                   ; filename (may carry a directory part)
    call path_enter              ; DX -> basename, CHDIR to its directory
    test ax, ax
    jz .sg_open
    mov word [v_rfd], -1         ; unknown directory: behave as not-found
    jmp .sg_ack
.sg_open:
    mov [v_name], dx
    xor bx, bx                   ; O_RDONLY
    call do_open
    mov [v_rfd], ax
    call path_leave              ; handle survives the CWD restore
.sg_ack:
    ; ACK the GET
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, op
    xor cx, cx
    call send_packet
    cmp word [v_rfd], 0
    jge .have
    ; File not found: CLOSE with zero CRC
    xor dx, dx
    xor ax, ax
    call crc_to_be
    mov bl, T_CLOSE
    mov bh, 0
    mov si, eb
    mov cx, 4
    call send_packet
    call wait_ack
    ret
.have:
    mov word [v_rcrc], 0
    mov word [v_rcrc+2], 0
    mov byte [v_dseq], 0
.loop:
    mov bx, [v_rfd]
    mov dx, fbuf
    mov cx, CHUNK
    call do_read
    cmp ax, 0
    jle .eof
    mov [v_got], ax
    mov si, fbuf
    mov cx, ax
    mov ax, [v_rcrc]
    mov dx, [v_rcrc+2]
    call crc32
    mov [v_rcrc], ax
    mov [v_rcrc+2], dx
    mov bl, T_DATA
    mov bh, [v_dseq]
    mov si, fbuf
    mov cx, [v_got]
    call send_packet
    call wait_ack
    inc byte [v_dseq]
    jmp .loop
.eof:
    mov bx, [v_rfd]
    call do_close
    mov ax, [v_rcrc]
    mov dx, [v_rcrc+2]
    call crc_to_be               ; eb[0..3] = CRC-32 big-endian
    mov bl, T_CLOSE
    mov bh, [v_dseq]
    mov si, eb
    mov cx, 4                    ; v0: CRC-32 only, no timestamps
    call send_packet
    call wait_ack
    ret

; serve_list(SI=spec at pk+2, AL=seq) -- enumerate a directory, emit T_ENTRY
; packets. The directory part of the spec (e.g. "DOCS" from "DOCS\*.*")
; selects the directory: the agent CHDIRs there, lists, and restores the
; original CWD afterwards. A spec with no directory part (plain "*.*")
; lists the current directory. The wildcard pattern itself is ignored
; (always list all entries); an unknown directory yields an empty listing.
serve_list:
    mov [v_seq], al
    ; ACK the LIST request first
    mov bl, T_ACK
    mov bh, [v_seq]
    mov si, op
    xor cx, cx
    call send_packet

    ; Parse the spec at pk+2: convert '\' -> '/', remember last separator
    mov byte [v_didcd], 0
    mov di, pk+2
    xor bx, bx                   ; BX = ptr to last separator (0 = none)
.sl_scan:
    mov al, [di]
    test al, al
    jz .sl_scandone
    cmp al, '\'
    jne .sl_nbs
    mov al, '/'
    mov [di], al
.sl_nbs:
    cmp al, '/'
    jne .sl_nsep
    mov bx, di
.sl_nsep:
    inc di
    jmp .sl_scan
.sl_scandone:
    test bx, bx
    jz .sl_open                  ; no directory part: list current directory
    cmp bx, pk+2
    jne .sl_trunc
    mov byte [bx+1], 0           ; leading '/' only: target is the root
    jmp .sl_cd
.sl_trunc:
    mov byte [bx], 0             ; cut the pattern off at the separator
.sl_cd:
    ; Save the CWD, then CHDIR to the requested directory
    mov ah, SYSCALL_GETCWD
    xor al, al
    xor bx, bx
    mov cx, v_cwdsave
    mov dx, 64
    xor si, si
    xor di, di
    int 0x40
    mov ah, SYSCALL_CHDIR
    xor al, al
    mov bx, pk+2
    xor cx, cx
    xor dx, dx
    xor si, si
    xor di, di
    int 0x40
    test ax, ax
    jnz .sl_done                 ; no such directory: empty listing
    mov byte [v_didcd], 1

.sl_open:
    ; Open the (now current) directory
    mov ah, SYSCALL_OPENDIR
    xor al, al
    xor bx, bx
    mov cx, dir_buf
    xor dx, dx
    xor si, si
    xor di, di
    int 0x40
    test ax, ax
    jnz .sl_done             ; opendir failed; send T_CLOSE immediately

.sl_loop:
    ; Read next entry
    mov ah, SYSCALL_READDIR
    xor al, al
    xor bx, bx
    mov cx, dir_buf
    mov dx, dirent_buf
    xor si, si
    xor di, di
    int 0x40
    test ax, ax
    jnz .sl_closedir         ; EOF or error

    ; Skip: deleted (0xE5), unused (0x00), dot entries ('.')
    mov al, [dirent_buf]
    cmp al, 0xE5
    je .sl_loop
    cmp al, 0x00
    je .sl_closedir
    cmp al, '.'
    je .sl_loop
    ; Skip: volume labels (attr 0x08) and LFN entries (attr 0x0F)
    mov al, [dirent_buf + D_ATTRS]
    test al, 0x08
    jnz .sl_loop
    cmp al, 0x0F
    je .sl_loop

    ; Build T_ENTRY header into eb[0..8]
    mov al, [dirent_buf + D_ATTRS]
    mov [eb + 0], al             ; attrs
    mov al, [dirent_buf + D_FSIZE + 0]
    mov [eb + 1], al             ; file_size LE byte 0
    mov al, [dirent_buf + D_FSIZE + 1]
    mov [eb + 2], al
    mov al, [dirent_buf + D_FSIZE + 2]
    mov [eb + 3], al
    mov al, [dirent_buf + D_FSIZE + 3]
    mov [eb + 4], al
    mov al, [dirent_buf + D_TIME + 0]
    mov [eb + 5], al             ; packed time LE
    mov al, [dirent_buf + D_TIME + 1]
    mov [eb + 6], al
    mov al, [dirent_buf + D_DATE + 0]
    mov [eb + 7], al             ; packed date LE
    mov al, [dirent_buf + D_DATE + 1]
    mov [eb + 8], al
    ; Build "NAME.EXT" NUL-terminated string into eb[9..]
    call build_name83
    ; Count strlen(eb+9) for total payload size
    mov si, eb + 9
    xor cx, cx
.sl_nlen:
    cmp byte [si], 0
    je .sl_nlen_done
    inc si
    inc cx
    jmp .sl_nlen
.sl_nlen_done:
    add cx, 9                    ; header(9) + name_strlen
    mov bl, T_ENTRY
    mov bh, 0
    mov si, eb
    call send_packet
    call wait_ack
    jmp .sl_loop

.sl_closedir:
    mov ah, SYSCALL_CLOSEDIR
    xor al, al
    xor bx, bx
    mov cx, dir_buf
    xor dx, dx
    xor si, si
    xor di, di
    int 0x40
.sl_done:
    ; Restore the original CWD if we changed it
    cmp byte [v_didcd], 0
    je .sl_norestore
    mov ah, SYSCALL_CHDIR
    xor al, al
    mov bx, v_cwdsave
    xor cx, cx
    xor dx, dx
    xor si, si
    xor di, di
    int 0x40
.sl_norestore:
    mov bl, T_CLOSE
    mov bh, 0
    mov si, op
    xor cx, cx
    call send_packet
    call wait_ack
    ret

; path_enter(DX = NUL-terminated path) -> DX = basename, AX = 0 ok / nonzero
; bad directory. Converts '\' -> '/' in place. When the path carries a
; directory part, saves the CWD in v_cwdsave, splits the string at the last
; separator and CHDIRs to the directory part (a leading separator alone means
; the root); v_didcd=1 tells path_leave to restore. A path with no separator
; is returned unchanged and no CHDIR happens. The kernel resolves paths one
; level below the root, so deeper directories fail (AX nonzero, CWD intact).
path_enter:
    mov byte [v_didcd], 0
    mov di, dx
    xor bx, bx                   ; BX = ptr to last separator (0 = none)
.pe_scan:
    mov al, [di]
    test al, al
    jz .pe_scandone
    cmp al, '\'
    jne .pe_nbs
    mov al, '/'
    mov [di], al
.pe_nbs:
    cmp al, '/'
    jne .pe_nsep
    mov bx, di
.pe_nsep:
    inc di
    jmp .pe_scan
.pe_scandone:
    test bx, bx
    jz .pe_none                  ; no directory part
    mov si, dx                   ; SI = directory string (= whole path)
    mov dx, bx
    inc dx                       ; DX = basename (past the separator)
    push dx
    cmp bx, si
    jne .pe_trunc
    mov si, rootstr              ; leading separator only: dir is the root
    jmp .pe_cd
.pe_trunc:
    mov byte [bx], 0             ; split dir part from basename
.pe_cd:
    push si
    mov ah, SYSCALL_GETCWD       ; save the CWD for path_leave
    xor al, al
    xor bx, bx
    mov cx, v_cwdsave
    mov dx, 64
    xor si, si
    xor di, di
    int 0x40
    pop bx                       ; BX = directory string
    mov ah, SYSCALL_CHDIR
    xor al, al
    xor cx, cx
    xor dx, dx
    xor si, si
    xor di, di
    int 0x40
    pop dx                       ; DX = basename
    test ax, ax
    jnz .pe_ret                  ; CHDIR failed: CWD unchanged, AX nonzero
    mov byte [v_didcd], 1
    ret                          ; AX = 0
.pe_none:
    xor ax, ax
.pe_ret:
    ret

; path_leave -- restore the CWD saved by path_enter (no-op when it did not
; CHDIR). Preserves all registers.
path_leave:
    cmp byte [v_didcd], 0
    je .pl_done
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, SYSCALL_CHDIR
    xor al, al
    mov bx, v_cwdsave
    xor cx, cx
    xor dx, dx
    xor si, si
    xor di, di
    int 0x40
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov byte [v_didcd], 0
.pl_done:
    ret

; build_name83 -- convert dirent_buf name[8]+ext[3] (space-padded) to
; "NAME.EXT" NUL-terminated string written into eb+9.
; Preserves all registers.
build_name83:
    push si
    push di
    push cx
    push ax
    mov si, dirent_buf
    mov di, eb + 9
    mov cx, 8
.bn_name:
    mov al, [si]
    inc si
    cmp al, ' '
    je .bn_after_name
    mov [di], al
    inc di
    loop .bn_name
.bn_after_name:
    ; Check if extension is non-blank
    mov si, dirent_buf + 8
    mov al, [si]
    cmp al, ' '
    je .bn_done
    mov byte [di], '.'
    inc di
    mov cx, 3
.bn_ext:
    mov al, [si]
    inc si
    cmp al, ' '
    je .bn_done
    mov [di], al
    inc di
    loop .bn_ext
.bn_done:
    mov byte [di], 0             ; NUL-terminate
    pop ax
    pop cx
    pop di
    pop si
    ret

; ===========================================================================
; OS-boundary primitives (rewritten for Harvac INT 0x40)
; ===========================================================================

; do_open(DX=path_offset, BX=flags) -> AX=handle or -1.
; flags: bit 8 (0x100) = O_CREAT -> use SYSCALL_CREATE;
;        low 2 bits = access mode (0=RDONLY, 1=WRONLY, 2=RDWR).
do_open:
    test bx, 0x0100
    jz .existing
    ; Create: SYSCALL_CREATE (0x15), BX=name_offset
    mov bx, dx
    mov ah, SYSCALL_CREATE
    xor al, al
    int 0x40
    cmp ax, 0xFFFF
    je .err
    ret
.existing:
    ; Open: SYSCALL_OPEN (0x10), AL=access_mode, BX=name_offset
    mov al, bl
    and al, 0x03
    mov bx, dx
    mov ah, SYSCALL_OPEN
    int 0x40
    cmp ax, 0xFFFF
    je .err
    ret
.err:
    mov ax, -1
    ret

; do_close(BX=handle).
do_close:
    mov ah, SYSCALL_CLOSE
    xor al, al
    int 0x40
    ret

; do_read(BX=handle, DX=buf_offset, CX=count) -> AX=bytes.
; Note: INT 0x40 SYSCALL_READ uses CX=buf, DX=count (opposite of xfercom convention).
do_read:
    push cx
    mov cx, dx      ; CX = buf_offset (was DX)
    pop dx          ; DX = count (was CX)
    mov ah, SYSCALL_READ
    xor al, al
    int 0x40
    ret

; do_write(BX=handle, DX=buf_offset, CX=count) -> AX=bytes.
do_write:
    push cx
    mov cx, dx      ; CX = buf_offset
    pop dx          ; DX = count
    mov ah, SYSCALL_WRITE
    xor al, al
    int 0x40
    ret

; do_lseek(BX=handle, CX=offset_high, DX=offset_low) -> SEEK_SET.
; Harvac SEEK: CX=pos_low, DX=pos_high -- so we swap CX and DX.
do_lseek:
    xchg cx, dx
    mov ah, SYSCALL_SEEK
    xor al, al
    int 0x40
    ret

; do_mkdir(DX=path_offset) -> AX=0/-1.
do_mkdir:
    mov bx, dx
    mov ah, SYSCALL_MKDIR
    xor al, al
    int 0x40
    test ax, ax
    jz .ok
    mov ax, -1
    ret
.ok:
    xor ax, ax
    ret

; do_delete(DX=path_offset) -> AX=0/-1.
do_delete:
    mov bx, dx
    mov ah, SYSCALL_DELETE
    xor al, al
    int 0x40
    test ax, ax
    jz .ok
    mov ax, -1
    ret
.ok:
    xor ax, ax
    ret

; do_rmdir(DX=path_offset) -> AX=0/-1.
do_rmdir:
    mov bx, dx
    mov ah, SYSCALL_RMDIR
    xor al, al
    int 0x40
    test ax, ax
    jz .ok
    mov ax, -1
    ret
.ok:
    xor ax, ax
    ret

; do_rename(DX=old_name_offset, DI=new_name_offset) -> AX=0/-1.
do_rename:
    mov si, dx
    mov ah, SYSCALL_RENAME
    xor al, al
    int 0x40
    test ax, ax
    jz .ok
    mov ax, -1
    ret
.ok:
    xor ax, ax
    ret

; putstr(SI=asciiz) -- write string to VGA screen (not serial).
putstr:
    push ax
    xor ax, ax
    mov ah, SYSCALL_WRITE_VGA
    int 0x40
    pop ax
    ret

; puts(SI=asciiz) -- write string + CRLF to VGA screen.
puts:
    call putstr
    push si
    mov si, msg_crlf
    call putstr
    pop si
    ret

; ===========================================================================
; UART primitives (verbatim from xfercom.asm, direct port I/O)
; ===========================================================================

; parse_args -- read PSP command tail, populate v_base/v_div/v_com/v_baudstr.
parse_args:
    mov word [v_base], 0x3F8
    mov word [v_div],  12        ; 115200/12 = 9600
    mov byte [v_com],  1
    mov word [v_baudstr], msg_9600

    mov si, 0x82                 ; Harvac EXEC puts args at PSP+0x82 (not +0x81)

.pa_skip1:
    mov al, [si]
    test al, al                  ; NUL = end of args (Harvac convention)
    jz .pa_done
    cmp al, 0x0D
    je .pa_done
    cmp al, ' '
    je .pa_skip1x
    cmp al, 0x09
    je .pa_skip1x
    jmp .pa_baud_start
.pa_skip1x:
    inc si
    jmp .pa_skip1

.pa_baud_start:
    mov al, [si]
    test al, al                  ; NUL = no baud number, use default
    jz .pa_done
    cmp al, '0'
    jb .pa_err
    cmp al, '9'
    ja .pa_err

    mov word [v_baud32],   0
    mov word [v_baud32+2], 0
    mov di, v_baudstr_buf

.pa_baud_digit:
    mov al, [si]
    cmp al, '0'
    jb .pa_baud_end
    cmp al, '9'
    ja .pa_baud_end
    mov [di], al
    inc di
    mov ax, [v_baud32]
    mov cx, 10
    mul cx
    mov bx, dx
    mov [v_baud32], ax
    mov ax, [v_baud32+2]
    mul cx
    add ax, bx
    mov [v_baud32+2], ax
    mov al, [si]
    sub al, '0'
    xor ah, ah
    add [v_baud32], ax
    adc word [v_baud32+2], 0
    inc si
    jmp .pa_baud_digit

.pa_baud_end:
    mov byte [di], 0
    mov word [v_baudstr], v_baudstr_buf

    mov ax, [v_baud32+2]
    test ax, ax
    jnz .pa_baud_hi

    mov ax, [v_baud32]
    cmp ax, 2
    jb .pa_err
    mov dx, 1
    mov ax, 0xC200               ; DX:AX = 115200
    div word [v_baud32]
    mov [v_div], ax
    jmp .pa_baud_ok

.pa_baud_hi:
    cmp ax, 1
    jne .pa_err
    mov ax, [v_baud32]
    cmp ax, 0xC200
    jne .pa_err
    mov word [v_div], 1

.pa_baud_ok:
.pa_skip2:
    mov al, [si]
    test al, al                  ; NUL = end of args, no COM number
    jz .pa_done
    cmp al, 0x0D
    je .pa_done
    cmp al, ' '
    je .pa_skip2x
    cmp al, 0x09
    je .pa_skip2x
    jmp .pa_com_start
.pa_skip2x:
    inc si
    jmp .pa_skip2

.pa_com_start:
    mov al, [si]
    test al, al                  ; NUL = no COM number, use COM1
    jz .pa_done
    cmp al, '0'
    jb .pa_err
    cmp al, '9'
    ja .pa_err
    sub al, '0'
    mov [v_com], al
    inc si
    mov al, [si]
    cmp al, 0x0D
    je .pa_com_ok
    cmp al, ' '
    je .pa_trail
    cmp al, 0x09
    je .pa_trail
    jmp .pa_err

.pa_trail:
    inc si
.pa_trail_lp:
    mov al, [si]
    test al, al                  ; NUL = end of args (Harvac convention)
    jz .pa_com_ok
    cmp al, 0x0D
    je .pa_com_ok
    cmp al, ' '
    je .pa_trail_lp_x
    cmp al, 0x09
    je .pa_trail_lp_x
    jmp .pa_err
.pa_trail_lp_x:
    inc si
    jmp .pa_trail_lp

.pa_com_ok:
    mov al, [v_com]
    cmp al, 1
    jb .pa_err
    cmp al, 4
    ja .pa_err
    dec al
    xor ah, ah
    shl ax, 1
    mov bx, com_tbl
    add bx, ax
    mov ax, [bx]
    mov [v_base], ax

.pa_done:
    ret

.pa_err:
    mov si, msg_usage
    call puts
    mov ah, SYSCALL_EXIT
    xor al, al
    int 0x40

; uart_init() -- 8N1, runtime baud (v_div) and port (v_base), FIFOs on, IRQs off.
uart_init:
    push ax
    push bx
    push dx
    mov bx, [v_base]
    lea dx, [bx+1]
    xor al, al
    out dx, al                   ; IER = 0 (interrupts off)
    lea dx, [bx+3]
    mov al, 0x80
    out dx, al                   ; LCR: set DLAB
    mov dx, bx
    mov ax, [v_div]
    out dx, al                   ; DLL (low byte of divisor)
    lea dx, [bx+1]
    mov al, ah
    out dx, al                   ; DLM (high byte of divisor)
    lea dx, [bx+3]
    mov al, 0x03
    out dx, al                   ; LCR: 8N1, clear DLAB
    lea dx, [bx+2]
    mov al, 0xC7
    out dx, al                   ; FCR: enable + clear FIFOs
    lea dx, [bx+4]
    mov al, 0x0B
    out dx, al                   ; MCR: DTR, RTS, OUT2
    pop dx
    pop bx
    pop ax
    ret

; uart_getc() -> AL=byte.  Blocks until RX ready.
; While waiting, polls PS/2 port 0x60 for 'Q' (scancode 0x10) to abort.
uart_getc:
    push dx
    push bx
    mov bx, [v_base]
.wait:
    ; Ask the kernel for a buffered key: the kernel keyboard driver is
    ; IRQ1-driven and consumes scancodes, so polling ports 0x64/0x60
    ; directly never sees them (and 0x64 does not exist on XT machines).
    ; KEY_AVAIL/READ_CHAR only touch the keyboard buffer, never the UART.
    mov ah, SYSCALL_KEY_AVAIL
    int 0x40
    test ax, ax
    jz .nokey
    mov ah, SYSCALL_READ_CHAR
    int 0x40                     ; AL = ascii ('q'/'Q' for the Q key)
    cmp al, 'q'
    je do_quit
    cmp al, 'Q'
    je do_quit
.nokey:
    lea dx, [bx+5]
    in al, dx                    ; LSR
    test al, 1                   ; RX data ready?
    jz .wait
    mov dx, bx
    in al, dx                    ; RBR: receive byte
    xor ah, ah
    pop bx
    pop dx
    ret

; uart_putc(AL=byte) -- blocks until THR empty, then transmits.
uart_putc:
    push ax
    push cx
    push dx
    push bx
    mov cl, al                   ; save byte (BX needed for port base)
    mov bx, [v_base]
.wait:
    lea dx, [bx+5]
    in al, dx                    ; LSR
    test al, 0x20                ; THR empty?
    jz .wait
    mov dx, bx
    mov al, cl
    out dx, al
    pop bx
    pop dx
    pop cx
    pop ax
    ret

; ===========================================================================
; Strings and lookup tables
; ===========================================================================
msg_rdyA  db "xfer ready on COM", 0
msg_rdyB  db " at ", 0
msg_rdyC  db " baud - press Q to quit", 0
msg_9600  db "9600", 0
msg_usage db "usage: XFER [baud [com]]  baud 2..115200, com 1..4", 0
msg_crlf  db 0x0D, 0x0A, 0
rootstr   db '/', 0
com_tbl   dw 0x3F8, 0x2F8, 0x3E8, 0x2E8

; ===========================================================================
; BSS: uninitialized variables declared as equ offsets past the code image.
; NASM emits no bytes for these -- the COM owns the full 64 KB segment.
; ===========================================================================
absbss          equ $
v_fd            equ absbss          ; word  -- open write handle (-1 = none)
v_wcrc          equ v_fd + 2        ; dword -- whole-file CRC-32 accumulator
v_n             equ v_wcrc + 4      ; word  -- decoded packet length
v_got16         equ v_n + 2         ; word  -- CRC extracted from packet tail
v_type          equ v_got16 + 2     ; byte  -- packet type
v_seq           equ v_type + 1      ; byte  -- packet sequence number
v_dlen          equ v_seq + 1       ; word  -- packet data length (n-4)
v_status        equ v_dlen + 2      ; byte  -- CLOSE status (0=ok, 1=crc mismatch)
v_name          equ v_status + 1    ; word  -- scratch near pointer
v_rfd           equ v_name + 2      ; word  -- read-side file handle
v_rcrc          equ v_rfd + 2       ; dword -- GET file CRC-32 accumulator
v_dseq          equ v_rcrc + 4      ; byte  -- DATA sequence counter for GET
v_got           equ v_dseq + 1      ; word  -- last read/write byte count

rxf             equ v_got + 2       ; 600 bytes -- raw UART receive buffer
pk              equ rxf + 600       ; 600 bytes -- COBS-decoded packet buffer
tx              equ pk + 600        ; 600 bytes -- COBS-encoded transmit buffer
op              equ tx + 600        ; 600 bytes -- outgoing packet before encoding
fbuf            equ op + 600        ; 128 bytes -- file read/write chunk buffer
eb              equ fbuf + CHUNK    ;  32 bytes -- scratch encode buffer / T_ENTRY

; Harvac-specific: directory iteration state
dir_buf         equ eb + 32         ;  16 bytes -- fat16_dir_t (struct is 14 bytes)
dirent_buf      equ dir_buf + 16    ;  32 bytes -- fat16_dirent_t

; Banner single-char COM port digit ("1".."4" + NUL)
v_cmbuf         equ dirent_buf + 32 ;   2 bytes

; parse_args runtime state (no emitted bytes)
v_base          equ v_cmbuf + 2     ; word  -- UART base port
v_div           equ v_base + 2      ; word  -- UART baud divisor
v_com           equ v_div + 2       ; byte  -- COM number 1..4 (for banner)
v_baud32        equ v_com + 1       ; dword -- baud accumulator (parse_args)
v_baudstr       equ v_baud32 + 4    ; word  -- near ptr to asciiz baud string
v_baudstr_buf   equ v_baudstr + 2   ;   8 bytes -- buffer for user baud digits

; serve_list directory selection state
v_didcd         equ v_baudstr_buf + 8 ; byte -- 1 = CHDIR'd away, restore after
v_cwdsave       equ v_didcd + 1     ;  64 bytes -- saved CWD during LIST
