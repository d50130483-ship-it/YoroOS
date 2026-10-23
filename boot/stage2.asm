/* YoroOS 0.2 stage2: self-loads the rest of itself (2 KiB), probes RAM, selects
 * optimal VESA VBE linear framebuffer mode (24/32 -> 15/16 -> 8 bpp), enables
 * A20, streams kernel of ANY size into extended memory at 0x00100000 (1 MiB)
 * via safe 16 KiB real-mode bounce batches, and jumps into 32-bit YoroKernel.
 */
.code16
.intel_syntax noprefix
.section .text, "ax"

.macro SER ch
    push    ax
    push    dx
    mov     dx, 0x3F8
    mov     al, \ch
    out     dx, al
    pop     dx
    pop     ax
.endm

.macro VBETRY slot
    mov     cx, [\slot]
    cmp     cx, 0xFFFF
    je      9f
    mov     bx, cx
    or      bx, 0x4000
    mov     ax, 0x4F02
    int     0x10
    cmp     ax, 0x004F
    jne     9f
    push    es
    xor     ax, ax
    mov     es, ax
    mov     ax, 0x4F01
    mov     di, 0x6000
    int     0x10
    pop     es
    cmp     ax, 0x004F
    jne     9f
    jmp     vbe_ok
9:
.endm

.equ P_SHIFT,  0x540
.equ P_EXT,    0x542
.equ P_STG2,   0x544
.equ P_SPT,    0x548
.equ P_HEADS,  0x54A
.equ P_SECS,   0x54C
.equ P_KERNEL, 0x550

.equ R_LBA,    0x602
.equ R_DEST,   0x606
.equ R_COUNT,  0x60A
.equ R_SPT,    0x60C
.equ R_HEADS,  0x60E
.equ R_OFF,    0x610
.equ R_EXT,    0x612
.equ R_HEAD,   0x614
.equ R_CYL,    0x616
.equ R_SEC,    0x618
.equ R_BATCH,  0x61A
.equ R_DAP,    0x61C
.equ R_SHIFT,  0x62C
.equ R_RETRY,  0x62D
.equ R_REM,    0x630
.equ R_HIADDR, 0x634
.equ R_CHUNK,  0x638

.globl _stage2
_stage2:
    .ascii "YS2!"
    jmp     boot_start

boot_start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    sti
    mov     byte ptr [0x600], dl
    call    serial_init
    SER     'S'
    SER     '2'
    call    self_load
    jnc     entered
    cli
hang0:
    hlt
    jmp     hang0

entered:
    SER     'V'
    jmp     stage2_main

self_load:
    cmp     word ptr [P_SECS], 0
    jne     .Lsl_go
    clc
    ret
.Lsl_go:
    mov     ax, [P_SHIFT]
    mov     [R_SHIFT], al
    mov     al, [P_EXT]
    mov     [R_EXT], al
    mov     ax, [P_SPT]
    mov     [R_SPT], ax
    mov     ax, [P_HEADS]
    mov     [R_HEADS], ax
    mov     ax, [P_SECS]
    mov     [R_COUNT], ax
    mov     eax, [P_STG2]
    mov     [R_LBA], eax
    mov     dword ptr [R_DEST], 0x8000
    jmp     read_sectors

serial_init:
    mov     dx, 0x3F8 + 1
    xor     al, al
    out     dx, al
    mov     dx, 0x3F8 + 3
    mov     al, 0x80
    out     dx, al
    mov     dx, 0x3F8
    mov     al, 1
    out     dx, al
    mov     dx, 0x3F8 + 1
    xor     al, al
    out     dx, al
    mov     dx, 0x3F8 + 3
    mov     al, 3
    out     dx, al
    ret

read_sectors:
    pusha
    push    es
rs_loop:
    mov     ax, [R_COUNT]
    test    ax, ax
    jz      rs_done

    mov     cl, [R_SHIFT]
    mov     eax, 0x8000
    shr     eax, cl
    movzx   edx, word ptr [R_COUNT]
    cmp     eax, edx
    jbe     .Lb4
    mov     eax, edx
.Lb4:
    mov     [R_BATCH], ax

    cmp     byte ptr [R_EXT], 0
    je      rs_chs

    mov     byte ptr [R_DAP], 16
    mov     byte ptr [R_DAP + 1], 0
    mov     ax, [R_BATCH]
    mov     [R_DAP + 2], ax
    mov     eax, [R_DEST]
    mov     ebx, eax
    and     bx, 0x0F
    mov     [R_DAP + 4], bx
    shr     eax, 4
    mov     [R_DAP + 6], ax
    mov     eax, [R_LBA]
    mov     [R_DAP + 8], eax
    mov     dword ptr [R_DAP + 12], 0
    mov     si, R_DAP
    mov     dl, [0x600]
    mov     ah, 0x42
    int     0x13
    jnc     rs_ok

    xor     ax, ax
    mov     dl, [0x600]
    int     0x13
    inc     byte ptr [R_RETRY]
    cmp     byte ptr [R_RETRY], 3
    jb      rs_loop
    mov     byte ptr [R_EXT], 0
    mov     byte ptr [R_RETRY], 0
    cmp     word ptr [R_SPT], 0
    je      rs_fail
    jmp     rs_loop

rs_chs:
    mov     bx, [R_SPT]
    test    bx, bx
    jz      rs_fail
    mov     eax, [R_LBA]
    xor     edx, edx
    movzx   ebx, bx
    div     ebx
    inc     dx
    mov     [R_SEC], dx
    mov     bx, [R_SPT]
    sub     bx, [R_SEC]
    inc     bx
    cmp     [R_BATCH], bx
    jbe     .Lc1
    mov     [R_BATCH], bx
.Lc1:
    mov     bx, [R_HEADS]
    test    bx, bx
    jz      rs_fail
    xor     dx, dx
    div     bx
    mov     [R_HEAD], dl
    mov     [R_CYL], ax

    mov     eax, [R_DEST]
    mov     ebx, eax
    and     bx, 0x0F
    mov     [R_OFF], bx
    shr     eax, 4
    mov     es, ax

    mov     bx, [R_OFF]
    mov     ax, [R_CYL]
    mov     ch, al
    mov     cl, [R_SEC]
    mov     dh, [R_HEAD]
    mov     dl, [0x600]
    mov     al, [R_BATCH]
    mov     ah, 0x02
    int     0x13
    jnc     rs_ok

    xor     ax, ax
    mov     dl, [0x600]
    int     0x13
    inc     byte ptr [R_RETRY]
    cmp     byte ptr [R_RETRY], 3
    jb      rs_loop
    jmp     rs_fail

rs_ok:
    mov     byte ptr [R_RETRY], 0
    movzx   eax, word ptr [R_BATCH]
    add     dword ptr [R_LBA], eax
    mov     cl, [R_SHIFT]
    shl     eax, cl
    add     dword ptr [R_DEST], eax
    mov     ax, [R_BATCH]
    sub     [R_COUNT], ax
    jmp     rs_loop

rs_fail:
    pop     es
    popa
    stc
    ret
rs_done:
    pop     es
    popa
    clc
    ret

/* ---------------- everything below is loaded by self_load ---------------- */
stage2_main:
    /* ensure R_SHIFT/R_EXT/R_SPT/R_HEADS are populated even if self_load skipped */
    mov     ax, [P_SHIFT]
    mov     [R_SHIFT], al
    mov     al, [P_EXT]
    mov     [R_EXT], al
    mov     ax, [P_SPT]
    mov     [R_SPT], ax
    mov     ax, [P_HEADS]
    mov     [R_HEADS], ax

    mov     si, offset msg_stage2 + 0x8000
    call    print

    /* ---- memory size (int 15h e801) ---- */
    xor     cx, cx
    xor     dx, dx
    mov     ax, 0xE801
    int     0x15
    jc      mem_done
    test    ax, ax
    jnz     .Lm1
    mov     ax, cx
    mov     bx, dx
.Lm1:
    movzx   eax, ax
    movzx   ebx, bx
    shl     ebx, 6
    add     eax, ebx
    cmp     eax, 4096
    jae     .Lm2
    mov     eax, 4096
.Lm2:
    mov     [0x5030], eax
mem_done:

    /* ---- VBE mode discovery ---- */
    mov     dword ptr [0x6800], 0x32454256
    mov     di, 0x6800
    mov     ax, 0x4F00
    int     0x10
    cmp     ax, 0x004F
    jne     vbe_none
    cmp     dword ptr [0x6800], 0x41534556
    jne     vbe_none

    mov     word ptr [0x6320], 0xFFFF
    mov     dword ptr [0x6322], 0
    mov     word ptr [0x6328], 0xFFFF
    mov     dword ptr [0x632A], 0
    mov     word ptr [0x6330], 0xFFFF
    mov     dword ptr [0x6332], 0

    mov     bp, [0x680E]
    mov     ax, [0x6810]
    mov     es, ax
.Lenum:
    mov     cx, es:[bp]
    add     bp, 2
    cmp     cx, 0xFFFF
    je      .Lpick
    mov     [0x6340], bp
    mov     [0x6342], cx
    push    es
    xor     ax, ax
    mov     es, ax
    mov     ax, 0x4F01
    mov     di, 0x6000
    int     0x10
    pop     es
    mov     bp, [0x6340]
    mov     cx, [0x6342]
    cmp     ax, 0x004F
    jne     .Lenum

    test    word ptr [0x6000], 0x0001
    jz      .Lenum
    cmp     dword ptr [0x6028], 0
    je      .Lenum
    cmp     word ptr [0x6012], 640
    jb      .Lenum
    cmp     word ptr [0x6014], 480
    jb      .Lenum
    mov     al, [0x6019]
    cmp     al, 24
    jae     .Lslot_hi
    cmp     al, 15
    jae     .Lslot_mid
    cmp     al, 8
    jne     .Lenum
    mov     si, 0x6330
    cmp     word ptr [si], 0xFFFF
    jne     .Lenum
    mov     [si], cx
    jmp     .Lenum
.Lslot_hi:
    mov     si, 0x6320
    call    keep_biggest
    jmp     .Lenum
.Lslot_mid:
    mov     si, 0x6328
    call    keep_biggest
    jmp     .Lenum

.Lpick:
    VBETRY  0x6320
    VBETRY  0x6328
    VBETRY  0x6330
    jmp     vbe_fail

vbe_none:
    mov     si, offset msg_novbebios + 0x8000
    call    print
    jmp     vbe_hang

vbe_fail:
    mov     si, offset msg_novbe + 0x8000
    call    print
vbe_hang:
    cli
hang1:
    hlt
    jmp     hang1

vbe_ok:
    mov     dword ptr [0x5000], 0x4F524F59   /* 'YORO' */
    movzx   eax, word ptr [0x6012]
    mov     [0x5004], eax
    movzx   eax, word ptr [0x6014]
    mov     [0x5008], eax
    movzx   eax, word ptr [0x6010]
    mov     [0x500C], eax
    movzx   eax, byte ptr [0x6019]
    mov     [0x5010], eax
    mov     eax, [0x6028]
    mov     [0x5014], eax
    movzx   eax, byte ptr [0x6020]
    mov     [0x5018], eax
    movzx   eax, byte ptr [0x601F]
    mov     [0x501C], eax
    movzx   eax, byte ptr [0x6022]
    mov     [0x5020], eax
    movzx   eax, byte ptr [0x6021]
    mov     [0x5024], eax
    movzx   eax, byte ptr [0x6024]
    mov     [0x5028], eax
    movzx   eax, byte ptr [0x6023]
    mov     [0x502C], eax

    /* ---- Enable A20 (BIOS int15 + Fast A20) ---- */
    mov     ax, 0x2401
    int     0x15
    in      al, 0x92
    or      al, 0x02
    and     al, 0xFE
    out     0x92, al
    SER     'A'

    /* ---- Stream kernel (32-bit sector count at 0x7C00 + 0x1B4) to 0x100000 ---- */
    mov     eax, [0x7C00 + 0x1B4]
    test    eax, eax
    jz      no_kernel
    shl     eax, 9
    mov     cl, [R_SHIFT]
    mov     edx, 1
    shl     edx, cl
    dec     edx
    add     eax, edx
    shr     eax, cl                          /* total device sectors */
    mov     [R_REM], eax
    mov     eax, [P_KERNEL]
    mov     [R_LBA], eax
    mov     dword ptr [R_HIADDR], 0x00100000

.Lkloop:
    mov     eax, [R_REM]
    test    eax, eax
    jz      .Lkdone
    /* max 16 KiB per bounce chunk (32 x 512B sectors, or 8 x 2048B CD sectors) */
    mov     edx, 0x4000
    mov     cl, [R_SHIFT]
    shr     edx, cl
    cmp     eax, edx
    jbe     .Lk1
    mov     eax, edx
.Lk1:
    mov     [R_CHUNK], ax
    mov     [R_COUNT], ax
    mov     dword ptr [R_DEST], 0x10000
    mov     byte ptr [R_RETRY], 0
    call    read_sectors
    jc      no_kernel

    /* Copy [0x10000 .. +bytes] -> [R_HIADDR] using protected-mode 4 GiB flat copy */
    cli
    lgdt    [gdt_desc + 0x8000]
    mov     eax, cr0
    or      eax, 1
    mov     cr0, eax
    mov     bx, 0x10
    mov     es, bx
    mov     ds, bx
    movzx   ebx, word ptr [R_CHUNK]
    mov     cl, [0x62C]                      /* R_SHIFT */
    shl     ebx, cl                          /* ebx = chunk byte count */
    mov     ecx, ebx
    shr     ecx, 2                           /* dwords */
    mov     esi, 0x10000
    mov     edi, [R_HIADDR]
    .byte   0x67, 0xF3, 0x66, 0xA5           /* addr32 rep movsd */
    mov     eax, cr0
    and     eax, 0xFFFFFFFE
    mov     cr0, eax
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    sti

    add     [R_HIADDR], ebx
    movzx   eax, word ptr [R_CHUNK]
    sub     [R_REM], eax
    jmp     .Lkloop

.Lkdone:
    SER     'k'

    /* ---- Enter 32-bit Protected Mode ---- */
    cli
    lgdt    [gdt_desc + 0x8000]
    mov     eax, cr0
    or      eax, 1
    mov     cr0, eax
    .byte   0x66, 0xEA
    .long   pm_entry + 0x8000
    .word   0x08

no_kernel:
    mov     si, offset msg_nokernel + 0x8000
    call    print
    cli
hang2:
    hlt
    jmp     hang2

print:
    pusha
.Lpr1:
    lodsb
    test    al, al
    jz      .Lpr2
    mov     ah, 0x0E
    mov     bx, 0x0007
    int     0x10
    jmp     .Lpr1
.Lpr2:
    popa
    ret

keep_biggest:
    movzx   eax, word ptr [0x6012]
    movzx   ebx, word ptr [0x6014]
    imul    eax, ebx
    cmp     eax, 0x200000
    ja      .Lkb_out
    cmp     eax, 0xD0000                     /* prefer up to 1280x800 / 1024x768 */
    ja      .Lkb_big
    add     eax, 0x2000000
    jmp     .Lkb_score
.Lkb_big:
    shr     eax, 2
    neg     eax
    add     eax, 0x1000000
.Lkb_score:
    movzx   edx, byte ptr [0x6019]
    shl     edx, 24
    add     eax, edx
    cmp     word ptr [si], 0xFFFF
    je      .Lkb_store
    cmp     eax, [si + 2]
    jbe     .Lkb_out
.Lkb_store:
    mov     [si + 2], eax
    mov     [si], cx
.Lkb_out:
    ret

msg_stage2:    .asciz "YoroOS 0.2 stage2\r\n"
msg_novbe:     .asciz "YoroOS: no usable VBE linear-framebuffer mode\r\n"
msg_novbebios: .asciz "YoroOS: no VESA VBE support\r\n"
msg_nokernel:  .asciz "YoroOS: kernel load failed\r\n"

.align 8
gdt_start:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_end:
gdt_desc:
    .word gdt_end - gdt_start - 1
    .long gdt_start + 0x8000

.code32
pm_entry:
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax
    mov     esp, 0x90000
    xor     ebp, ebp
    SER     'P'
    .byte   0xEA
    .long   0x00100000
    .word   0x0008
