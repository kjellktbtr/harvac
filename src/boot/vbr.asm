; vbr.asm -- Volume Boot Record code for Harvac
; Placed at offset 0x3B by build script. Max 447 bytes.
; Loaded at physical 0x9000 by MBR.

bits 16
cpu 8086
org 0x003B

; --- FAT16 params (from build.py) ---
PART_START    equ 63
RESERVED      equ 1
FAT_COUNT     equ 2
FAT_SECTORS   equ 32
ROOT_SECTORS  equ 32
ROOT_START    equ PART_START + RESERVED + FAT_COUNT * FAT_SECTORS
FIRST_DATA    equ PART_START + RESERVED + FAT_COUNT * FAT_SECTORS + ROOT_SECTORS

; --- Entry ---
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti

    mov ax, 0x0900
    mov ds, ax
    mov es, ax

    ; Save boot drive from MBR
    mov dl, [0x01FF]
    mov [boot_drv], dl

    ; V = VBR entry reached
    mov al, 'V'
    call dbg_char

    ; Reset disk
    mov dl, [boot_drv]
    mov ah, 0x00
    int 0x13

    ; R = disk reset done (dbg_char restores DS and ES internally)
    mov al, 'R'
    call dbg_char

    ; Query BIOS geometry so CHS reads work on any controller (XTIDE, MFM, IDE).
    ; Fall back to hardcoded 63/255 if AH=08h is unsupported or returns garbage.
    ; AH=08h corrupts DS and ES; save/restore DS around the call.
    push ds
    mov ah, 0x08
    mov dl, [boot_drv]
    int 0x13
    pop ds                  ; restore DS=0x0900 before any memory write
    jc .geo_done            ; CF=1: not supported, keep defaults
    and cl, 0x3F            ; CL bits 5-0 = sectors per track
    jz .geo_done            ; SPT=0: garbage, keep defaults
    mov [num_sects], cl
    inc dh                  ; DH = max head (0-based), num_heads = DH+1
    mov [num_heads], dh
.geo_done:
    ; AH=08h may leave the controller in an unexpected state on some hardware
    ; (XTIDE in particular). Issue a second reset to clear any residual state.
    mov ah, 0x00
    mov dl, [boot_drv]
    int 0x13

    ; Search root directory for KERNEL.COM
    ; S = starting root directory search
    mov al, 'S'
    call dbg_char

    mov ax, ROOT_START
    mov cx, ROOT_SECTORS

.search:
    push ax
    push cx
    ; . = each root sector read attempt
    push ax
    mov al, '.'
    call dbg_char
    pop ax

    mov bx, 0x0200
    call read_sector

    test byte [read_ok], 1
    jz .derr

    ; Restore ES: INT 13h calls may corrupt it
    mov ax, 0x0900
    mov es, ax
    mov di, 0x0200
    mov dx, 16

.entries:
    push di
    push si
    mov si, kernel_name
    cld
    mov cx, 11
    repe cmpsb
    pop si
    pop di
    jne .next_entry

    ; Found KERNEL.COM - load it (cluster at FAT16 offset 0x1A)
    ; K = KERNEL.COM found
    mov al, 'K'
    call dbg_char

    mov dx, [di + 0x1A]
    pop cx
    pop ax
    mov ax, dx

    jmp .load_kernel

.next_entry:
    add di, 32
    dec dx
    jnz .entries

    pop cx
    pop ax
    inc ax
    loop .search

    ; Not found - H = halt (KERNEL.COM missing)
    mov al, 'H'
    call dbg_char
    jmp $

; --- Load kernel to 0x20000 ---
.load_kernel:
    ; L = starting kernel load (AX holds cluster number - must preserve AL)
    push ax
    mov al, 'L'
    call dbg_char
    pop ax

    mov bx, 0x0900
    mov es, bx
    xor cx, cx
    mov [loaded_bytes], cx

.loop:
    push ax
    push cx
    ; * = each kernel sector loaded
    push ax
    mov al, '*'
    call dbg_char
    pop ax

    mov cx, ax
    sub cx, 2
    mov bx, 0x0200
    add cx, FIRST_DATA
    mov ax, cx

    mov bx, 0x0200
    call read_sector
    test byte [read_ok], 1
    jz .derr

    mov si, 0x0200
    mov ax, 0x0900
    mov es, ax
    mov di, [loaded_bytes]
    mov bx, 0x2000
    mov es, bx
    cld
    mov cx, 256
    rep movsw

    mov ax, 0x0900
    mov es, ax
    mov ax, [loaded_bytes]
    add ax, 512
    mov [loaded_bytes], ax

    pop cx
    pop ax

    call fat_read
    cmp ax, 0xFFF8
    jae .done
    jmp .loop

.derr:
    ; E = read error
    mov al, 'E'
    call dbg_char
    pop cx
    pop ax
    jmp $

.done:
    ; J = jumping to kernel
    mov al, 'J'
    call dbg_char

    ; Direct far jump: push imm16 is 186+, misdecodes on real 8086/8088
    cli
    jmp 0x2000:0x0000

; --- dbg_char: print AL to COM1 (0x3F8) and VGA. Preserves all regs. ---
dbg_char:
    push ax
    push bx
    push dx
    mov dx, 0x3F8
    out dx, al
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    mov ax, 0x0900
    mov ds, ax
    mov es, ax
    pop dx
    pop bx
    pop ax
    ret

; --- read_sector: LBA in AX -> reads to DS:0x0200 ---
; Uses CHS via INT 13h AH=02h (compatible with all x86 BIOSes).
; Preserves AX, BX, CX, DX. Sets [read_ok]: 1=success, 0=error.
read_sector:
    push ax
    push bx
    push cx
    push dx

    ; Convert LBA to CHS.
    ; sector (1-based) = (LBA mod num_sects) + 1
    xor dx, dx
    xor bh, bh
    mov bl, [num_sects]
    div bx              ; AX = LBA/S, DX = LBA mod S
    mov [tmp_sec], dl   ; save 0-based sector index

    ; head = (LBA/S) mod num_heads, cylinder = (LBA/S) / num_heads
    xor dx, dx
    mov bl, [num_heads]
    div bx              ; AX = cylinder, DX = head

    mov ch, al          ; CH = cylinder (< 256 for 32MB disk)
    mov cl, [tmp_sec]
    inc cl              ; CL = sector (1-based)
    mov dh, dl          ; DH = head
    mov dl, [boot_drv]

    ; Set buffer segment explicitly: BIOS calls may have corrupted ES
    mov ax, 0x0900
    mov es, ax
    mov bx, 0x0200
    mov ah, 0x02
    mov al, 1

    mov byte [read_ok], 0
    int 0x13
    jc .rs_fail
    mov byte [read_ok], 1
.rs_fail:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- fat_read: FAT entry for cluster AX -> AX ---
fat_read:
    push bx
    push cx
    push dx
    push di

    ; FAT offset = cluster * 2
    mov bx, ax
    shl bx, 1

    ; sector = offset / 512, rem = offset % 512
    mov ax, bx
    xor dx, dx
    mov bx, 512
    div bx              ; AX = sector, DX = offset within sector
    mov di, dx          ; DI = offset within sector

    ; LBA = PART_START + RESERVED + sector
    add ax, PART_START + RESERVED

    ; Read FAT sector
    push di
    mov bx, 0x0200
    call read_sector
    pop di

    ; FAT entry at offset DI
    mov bx, di
    mov ax, [0x0200 + bx]

    pop di
    pop dx
    pop cx
    pop bx
    ret

; --- Data ---
kernel_name:  db "KERNEL  COM"
loaded_bytes: dw 0
boot_drv:     db 0
read_ok:      db 0
num_heads:    db 255
num_sects:    db 63
tmp_sec:      db 0
