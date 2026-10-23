/* YoroOS boot sector - 512 bytes, loaded by the BIOS at 0x7C00.
 * Loads stage2 (LBA 1..48) to 0x8000 and jumps to it. Uses int13h LBA
 * extensions when the BIOS has them, CHS with the reported geometry otherwise. */
.code16
.intel_syntax noprefix
.section .text, "ax"
.globl _boot_start

.equ DRIVE, 0x600
.equ EXT,   0x601
.equ RETRY, 0x602
.equ LBA,   0x604
.equ DEST,  0x608
.equ COUNT, 0x60C
.equ SPT,   0x610
.equ HEADS, 0x612
.equ TMP,   0x614
.equ DAP,   0x620

_boot_start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    sti

    mov     [DRIVE], dl
    mov     byte ptr [RETRY], 0
    mov     word ptr [SPT], 0
    mov     word ptr [HEADS], 0

    mov     si, offset msg_boot + 0x7C00
    call    print

    mov     bx, 0x55AA               /* int13h extensions? */
    mov     dl, [DRIVE]
    mov     ah, 0x41
    int     0x13
    jc      .Lnoext
    cmp     bx, 0xAA55
    jne     .Lnoext
    mov     byte ptr [EXT], 1
    jmp     .Lstart
.Lnoext:
    mov     byte ptr [EXT], 0

.Lstart:
    mov     dword ptr [LBA], 1
    mov     dword ptr [DEST], 0x8000
    mov     word ptr [COUNT], 48
    call    read_sectors
    jnc     start_stage2

    mov     si, offset msg_fail + 0x7C00
    call    print
    cli
.Lhalt:
    hlt
    jmp     .Lhalt

start_stage2:
    .byte   0xEA
    .word   0x8000
    .word   0x0000

print:
    pusha
.Lp1:
    lodsb
    test    al, al
    jz      .Lp2
    mov     ah, 0x0E
    mov     bx, 0x0007
    int     0x10
    jmp     .Lp1
.Lp2:
    popa
    ret

/* cache the floppy/disk geometry in SPT and HEADS */
read_geometry:
    cmp     word ptr [SPT], 0
    jne     .Ldone
    mov     dl, [DRIVE]
    mov     ah, 0x08
    int     0x13
    jc      .Ldef
    movzx   ax, cl
    and     ax, 0x3F
    jz      .Ldef
    mov     [SPT], ax
    movzx   ax, dh
    inc     ax
    mov     [HEADS], ax
.Ldone:
    ret
.Ldef:
    mov     word ptr [SPT], 63
    mov     word ptr [HEADS], 16
    ret

/* one sector at a time: LBA at LBA, destination linear at DEST, count at COUNT */
read_sectors:
    pusha
    push    es
.Lloop:
    cmp     word ptr [COUNT], 0
    je      .Lok_done

    cmp     byte ptr [EXT], 1
    jne     .Lchs

    /* --- LBA (int13h AH=42h) --- */
    mov     byte ptr [DAP], 16
    mov     byte ptr [DAP + 1], 0
    mov     word ptr [DAP + 2], 1
    mov     eax, [DEST]
    mov     ebx, eax
    and     bx, 0x0F
    mov     [DAP + 4], bx
    shr     eax, 4
    mov     [DAP + 6], ax
    mov     eax, [LBA]
    mov     [DAP + 8], eax
    mov     dword ptr [DAP + 12], 0
    mov     si, DAP
    mov     dl, [DRIVE]
    mov     ah, 0x42
    int     0x13
    jnc     .Lok
    mov     byte ptr [EXT], 0
    jmp     .Lloop

.Lchs:
    call    read_geometry
    mov     eax, [LBA]
    xor     edx, edx
    movzx   ebx, word ptr [SPT]
    test    ebx, ebx
    jz      .Lfail
    div     ebx
    inc     dl
    mov     [TMP], dx
    xor     edx, edx
    movzx   ebx, word ptr [HEADS]
    test    ebx, ebx
    jz      .Lfail
    div     ebx
    mov     [TMP + 2], dl
    mov     [TMP + 4], ax

    mov     eax, [DEST]
    mov     ebx, eax
    and     bx, 0x0F
    mov     [TMP + 6], bx
    shr     eax, 4
    mov     es, ax

    mov     bx, [TMP + 6]
    mov     ax, [TMP + 4]
    mov     ch, al
    mov     cl, [TMP]
    mov     dh, [TMP + 2]
    mov     dl, [DRIVE]
    mov     ax, 0x0201
    int     0x13
    jnc     .Lok

    xor     ax, ax
    mov     dl, [DRIVE]
    int     0x13
    inc     byte ptr [RETRY]
    cmp     byte ptr [RETRY], 3
    jb      .Lloop

.Lfail:
    pop     es
    popa
    stc
    ret

.Lok:
    mov     byte ptr [RETRY], 0
    inc     dword ptr [LBA]
    add     dword ptr [DEST], 512
    dec     word ptr [COUNT]
    jmp     .Lloop

.Lok_done:
    pop     es
    popa
    clc
    ret

msg_boot: .asciz "YoroOS\r\n"
msg_fail: .asciz "Boot error\r\n"

/* kernel sector count patched by tools/mkimage.py (offset 0x1F0) */
.org 0x1F0
.word 0
.org 0x1FE
.word 0xAA55
