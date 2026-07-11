; mbr.asm -- Master Boot Record for Harvac
; Loaded by BIOS at 0x7C00. Scans partition table for active partition,
; loads the VBR (first sector of active partition) to 0x9000, jumps to it.

bits 16
cpu 8086
org 0x7C00

; --- Entry point ---
_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    ; Reset disk controller before any disk access
    xor ah, ah
    int 0x13

    mov si, msg_booting
    call print_string

    ; Scan partition table for active partition
    mov si, PARTITION_TABLE
    mov cx, 4

find_active:
    cmp byte [si], 0x80
    je  load_vbr
    add si, 16
    loop find_active

; --- Load VBR from active partition ---
load_vbr:
    ; Query BIOS geometry so LBA 63 CHS is correct for any controller.
    ; AH=08h may corrupt DS; save/restore around the call.
    push ds
    mov ah, 0x08
    mov dl, [boot_drive]
    int 0x13
    pop ds
    jc .geo_done
    and cl, 0x3F            ; CL bits 5-0 = sectors per track
    jz .geo_done            ; SPT=0: garbage, keep defaults
    mov [num_sects], cl
    inc dh                  ; DH = max head (0-based), num_heads = DH+1
    mov [num_heads], dh
.geo_done:
    ; Compute CHS for LBA 63
    mov ax, 63
    xor dx, dx
    xor bh, bh
    mov bl, [num_sects]
    div bx                  ; AX = track, DX = sector (0-based)
    inc dl                  ; sector (1-based)
    mov [vbr_sec], dl
    xor dx, dx
    mov bl, [num_heads]
    div bx                  ; AX = cylinder, DX = head
    mov [vbr_cyl], al
    mov [vbr_hd], dl

    ; Retry up to 3 times with a disk reset between attempts.
    mov byte [retry_count], 3

.try_read:
    ; > = each VBR read attempt
    mov ah, 0x0E
    mov al, '>'
    mov bx, 0x0007
    int 0x10

    mov dl, [boot_drive]
    mov ax, 0x0900
    mov es, ax
    xor bx, bx                  ; ES:BX = 0x0900:0x0000
    mov ah, 0x02
    mov al, 1
    mov ch, [vbr_cyl]
    mov cl, [vbr_sec]
    mov dh, [vbr_hd]
    int 0x13
    jnc .vbr_ok

    ; ! = read failed, resetting and retrying
    mov ah, 0x0E
    mov al, '!'
    mov bx, 0x0007
    int 0x10

    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec byte [retry_count]
    jnz .try_read
    jmp disk_error

.vbr_ok:
    ; Store boot drive at known location for VBR
    mov dl, [boot_drive]
    mov byte [es:0x01FF], dl

    ; Jump to VBR at 0x0900:0x003B.
    ; Direct far jump: push imm16 is a 186+ instruction and misdecodes
    ; as a conditional jump on a real 8086/8088.
    jmp 0x0900:0x003B

disk_error:
    mov si, msg_disk_error
    call print_string
    jmp $

; --- Print string (BIOS teletype, null-terminated) ---
print_string:
    push es
    xor ax, ax
    mov es, ax
.p_loop:
    lodsb
    or al, al
    jz  .p_done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .p_loop
.p_done:
    pop es
    ret

; --- Data ---
boot_drive    db 0
retry_count   db 0
num_sects     db 63
num_heads     db 255
vbr_cyl       db 0
vbr_sec       db 1
vbr_hd        db 1

msg_booting   db "Harvac", 0
msg_disk_error db "Disk err", 0

; --- Partition table starts at offset 0x1BE ---
times 0x1BE - ($ - $$) db 0

PARTITION_TABLE:
    db 0x80
    db 0x01
    db 0x01
    db 0x00
    db 0x0B
    db 0xFF
    db 0x3F
    db 0x3F
    dd 63
    dd 8129                      ; 4MB image: 8192 total sectors - 63 hidden

    times 3 * 16 db 0

times 510 - ($ - $$) db 0
dw 0xAA55