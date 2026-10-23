/* YoroOS stage2: self-loads the rest of itself, sets a VBE mode, probes
 * memory, enables A20, loads the kernel and enters protected mode.
 *
 * The first sector is loaded by the boot sector to 0x8000 and starts with the
 * magic "YS2!".  Everything in this sector must therefore fit in 512 bytes:
 * the entry code, the self-loader and the BIOS read routine. */
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

/* boot parameters left by the boot sector */
.equ P_SHIFT,  0x540          /* word: 9 (512-byte) or 11 (2048-byte CD) */
.equ P_EXT,    0x542          /* word: int13h LBA available */
.equ P_STG2,   0x544          /* dword: stage2 LBA */
.equ P_SPT,    0x548
.equ P_HEADS,  0x54A
.equ P_SECS,   0x54C          /* word: stage2 size in device sectors */
.equ P_KERNEL, 0x550          /* dword: kernel LBA */

/* runtime variables */
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

.globl _stage2
_stage2:
    .ascii "YS2!"                 /* magic checked by the boot sector */
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

/* Re-read the whole first 2 KiB block at P_STG2 into 0x8000.  The boot sector
 * only copied one 512-byte sector, and the rest of stage2 does not start at a
 * device sector boundary on a 2048-byte CD, so reading the block again is the
 * cheapest way to get a contiguous image.  P_SECS is the number of device
 * sectors that cover those 2 KiB (4 on 512-byte media, 1 on a CD). */
self_load:
    cmp     word ptr [P_SECS], 0        /* 0: the boot sector already loaded 2 KiB */
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

/* read R_COUNT sectors from R_LBA to R_DEST, honouring R_SHIFT.
 * Falls back to CHS (with R_SPT / R_HEADS) when LBA reads are unavailable. */
read_sectors:
    pusha
    push    es
rs_loop:
    mov     ax, [R_COUNT]
    test    ax, ax
    jz      rs_done

    mov     cl, [R_SHIFT]
    movzx   eax, word ptr [R_DEST]
    neg     eax
    and     eax, 0xFFFF
    jnz     .Lb1
    mov     eax, 0x8000
.Lb1:
    shr     eax, cl
    jnz     .Lb2
    mov     eax, 1
.Lb2:
    mov     edx, 0x8000
    shr     edx, cl
    cmp     eax, edx
    jbe     .Lb3
    mov     eax, edx
.Lb3:
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
    mov     byte ptr [R_EXT], 0
    jmp     rs_loop

rs_chs:
    mov     bx, [R_SPT]
    test    bx, bx
    jz      rs_fail
    mov     ax, [R_LBA]
    xor     dx, dx
    div     bx                          /* ax = track, dx = sector index */
    inc     dx
    mov     [R_SEC], dx                 /* sector, 1-based */
    sub     bx, dx
    inc     bx                          /* sectors left on this track */
    cmp     [R_BATCH], bx
    jbe     .Lc1
    mov     [R_BATCH], bx
.Lc1:
    mov     bx, [R_HEADS]
    test    bx, bx
    jz      rs_fail
    xor     dx, dx                      /* dx:ax holds the track number only */
    div     bx                          /* ax = cylinder, dx = head */
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

    /* ---- pick a VBE linear framebuffer mode ---- */
    mov     si, offset mode_list + 0x8000
vbe_try:
    mov     cx, [si]
    cmp     cx, 0xFFFF
    je      vbe_fail
    mov     ax, 0x4F01
    mov     di, 0x6000
    push    si
    int     0x10
    pop     si
    cmp     ax, 0x004F
    jne     vbe_next
    cmp     byte ptr [0x6019], 32
    jb      vbe_next
    cmp     dword ptr [0x6028], 0
    je      vbe_next
    mov     bx, [si]
    or      bx, 0x4000
    mov     ax, 0x4F02
    push    si
    int     0x10
    pop     si
    cmp     ax, 0x004F
    jne     vbe_next
    jmp     vbe_ok
vbe_next:
    add     si, 2
    jmp     vbe_try

vbe_fail:
    mov     si, offset msg_novbe + 0x8000
    call    print
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

    /* ---- A20 ---- */
    in      al, 0x92
    or      al, 0x02
    and     al, 0xFE
    out     0x92, al
    SER     'A'

    /* ---- load the kernel (sector count patched at 0x7C00 + 0x1F0) ---- */
    movzx   eax, word ptr [0x7C00 + 0x1F0]
    shl     eax, 9
    test    eax, eax
    jz      no_kernel
    mov     cl, [R_SHIFT]
    mov     edx, 1
    shl     edx, cl
    dec     edx
    add     eax, edx
    shr     eax, cl
    mov     [R_COUNT], ax
    mov     eax, [P_KERNEL]
    mov     [R_LBA], eax
    mov     dword ptr [R_DEST], 0x10000
    mov     byte ptr [R_RETRY], 0
    call    read_sectors
    jc      no_kernel
    SER     'k'

    /* ---- protected mode ---- */
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

/* ---- data ---- */
mode_list:
    .word 0x144     /* 1024x768x32 */
    .word 0x143     /* 800x600x32  */
    .word 0x142     /* 640x480x32  */
    .word 0x144     /* retry 1024x768 in case the first call failed */
    .word 0x143
    .word 0x142

msg_stage2:   .asciz "YoroOS stage2\r\n"
msg_novbe:    .asciz "YoroOS: VBE linear framebuffer not available\r\n"
msg_nokernel: .asciz "YoroOS: kernel load failed\r\n"

.align 8
gdt_start:
    .quad 0
    .quad 0x00CF9A000000FFFF    /* code 0x08: base 0, limit 4G, 32-bit */
    .quad 0x00CF92000000FFFF    /* data 0x10 */
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
    .long   0x00010000
    .word   0x0008
