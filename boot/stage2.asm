/* YoroOS stage2: VBE mode setup, memory probe, A20, kernel load, protected mode. */
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
.globl _stage2

_stage2:
    cli
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
    mov     dx, 0x3F8
    mov     al, 'S'
    out     dx, al
    mov     al, '2'
    out     dx, al
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    sti
    mov     byte ptr [0x601], 0

    mov     si, offset msg_stage2 + 0x8000
    call    print

    /* ---- memory size (int 15h e801) ---- */
    xor     cx, cx
    xor     dx, dx
    mov     ax, 0xE801
    int     0x15
    jc      mem_done
    test    ax, ax
    jnz     1f
    mov     ax, cx
    mov     bx, dx
1:  movzx   eax, ax
    movzx   ebx, bx
    shl     ebx, 6                  /* 64K blocks -> KB */
    add     eax, ebx
    cmp     eax, 4096
    jae     2f
    mov     eax, 4096               /* sanity floor */
2:  mov     [0x5030], eax           /* memkb */
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
    cmp     byte ptr [0x6019], 32       /* bpp */
    jb      vbe_next
    cmp     dword ptr [0x6028], 0       /* phys base */
    je      vbe_next
    /* set mode with LFB bit */
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
    hlt
    jmp     vbe_fail

vbe_ok:
    mov     si, offset msg_vbe_ok + 0x8000
    call    print
    SER     'V' 
    mov     dword ptr [0x5000], 0x4F524F59   /* 'YORO' */
    movzx   eax, word ptr [0x6012]
    mov     [0x5004], eax               /* width */
    movzx   eax, word ptr [0x6014]
    mov     [0x5008], eax               /* height */
    movzx   eax, word ptr [0x6010]
    mov     [0x500C], eax               /* pitch */
    movzx   eax, byte ptr [0x6019]
    mov     [0x5010], eax               /* bpp */
    mov     eax, [0x6028]
    mov     [0x5014], eax               /* framebuffer */
    movzx   eax, byte ptr [0x6020]
    mov     [0x5018], eax               /* red pos */
    movzx   eax, byte ptr [0x601F]
    mov     [0x501C], eax               /* red size */
    movzx   eax, byte ptr [0x6022]
    mov     [0x5020], eax               /* green pos */
    movzx   eax, byte ptr [0x6021]
    mov     [0x5024], eax               /* green size */
    movzx   eax, byte ptr [0x6024]
    mov     [0x5028], eax               /* blue pos */
    movzx   eax, byte ptr [0x6023]
    mov     [0x502C], eax               /* blue size */

    /* ---- A20 ---- */
    call    kbc_a20
    SER     'A' 

    SER     'l' 
    /* geometry for the CHS fallback */
    mov     byte ptr [0x612], 0
    mov     bx, 0x55AA
    mov     dl, [0x600]
    mov     ah, 0x41
    int     0x13
    jc      geom_chs
    cmp     bx, 0xAA55
    jne     geom_chs
    mov     byte ptr [0x612], 1
    jmp     geom_done
geom_chs:
    mov     dl, [0x600]
    mov     ah, 0x08
    int     0x13
    movzx   ax, cl
    and     ax, 0x3F
    jz      geom_default
    mov     [0x60C], ax
    movzx   ax, dh
    inc     ax
    mov     [0x60E], ax
    jmp     geom_done
geom_default:
    mov     word ptr [0x60C], 63
    mov     word ptr [0x60E], 16
geom_done:

    /* ---- load kernel: sector count patched in boot sector at 0x7C00+0x1F0 ---- */
    movzx   eax, word ptr [0x7C00 + 0x1F0]
    test    eax, eax
    jz      no_kernel
    mov     [0x60A], ax
    mov     dword ptr [0x602], 128       /* kernel LBA */
    mov     dword ptr [0x606], 0x10000   /* load address */
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
    hlt
    jmp     no_kernel

/* ---- helpers (16-bit) ---- */
kbc_a20:
    /* fast A20 gate via system control port 0x92 */
    in      al, 0x92
    or      al, 0x02
    and     al, 0xFE
    out     0x92, al
    ret

print:
    pusha
1:  lodsb
    test    al, al
    jz      2f
    mov     ah, 0x0E
    mov     bx, 0x0007
    int     0x10
    jmp     1b
2:  popa
    ret

/* read sectors: LBA 0x602, dest 0x606, count 0x60A.
   Tries int13h LBA extensions first, falls back to CHS geometry, and moves
   up to 32 sectors per BIOS call (limited by the 64 KiB segment and track). */
read_sectors:
    pusha
    push    es
rs_loop:
    mov     ax, [0x60A]
    test    ax, ax
    jz      rs_done
    cmp     ax, 32
    jbe     .Lcap1
    mov     ax, 32
.Lcap1:
    movzx   ecx, word ptr [0x606]
    neg     ecx
    and     ecx, 0xFFFF
    shr     ecx, 9
    jnz     .Lcap2
    mov     ecx, 128
.Lcap2:
    cmp     ax, cx
    jbe     .Lcap3
    mov     ax, cx
.Lcap3:
    mov     [0x61A], ax

    cmp     byte ptr [0x612], 1
    jne     rs_chs
    mov     byte ptr [0x61C], 16
    mov     byte ptr [0x61D], 0
    mov     ax, [0x61A]
    mov     [0x61E], ax
    mov     eax, [0x606]
    mov     ebx, eax
    and     bx, 0x0F
    mov     [0x620], bx
    shr     eax, 4
    mov     [0x622], ax
    mov     eax, [0x602]
    mov     [0x624], eax
    mov     dword ptr [0x628], 0
    mov     si, 0x61C
    mov     dl, [0x600]
    mov     ah, 0x42
    int     0x13
    jnc     rs_ok
    mov     byte ptr [0x612], 0
    jmp     rs_loop

rs_chs:
    mov     eax, [0x602]
    xor     edx, edx
    movzx   ebx, word ptr [0x60C]
    test    ebx, ebx
    jz      rs_fail
    div     ebx
    inc     dx
    mov     [0x618], dx
    movzx   ecx, word ptr [0x60C]
    movzx   edi, dx
    dec     edi
    sub     ecx, edi
    mov     si, [0x61A]
    cmp     si, cx
    jbe     .Lchs1
    mov     [0x61A], cx
.Lchs1:
    xor     edx, edx
    movzx   ebx, word ptr [0x60E]
    test    ebx, ebx
    jz      rs_fail
    div     ebx
    mov     [0x614], dl
    mov     [0x616], ax

    mov     eax, [0x606]
    mov     ebx, eax
    and     bx, 0x0F
    mov     [0x610], bx
    shr     eax, 4
    mov     es, ax

    mov     bx, [0x610]
    mov     ax, [0x616]
    mov     ch, al
    mov     cl, [0x618]
    mov     dh, [0x614]
    mov     dl, [0x600]
    mov     al, [0x61A]
    mov     ah, 0x02
    int     0x13
    jnc     rs_ok

    xor     ax, ax
    mov     dl, [0x600]
    int     0x13
    inc     byte ptr [0x601]
    cmp     byte ptr [0x601], 3
    jb      rs_loop

rs_fail:
    pop     es
    popa
    stc
    ret
rs_ok:
    mov     byte ptr [0x601], 0
    movzx   eax, word ptr [0x61A]
    add     dword ptr [0x602], eax
    shl     eax, 9
    add     dword ptr [0x606], eax
    mov     ax, [0x61A]
    sub     [0x60A], ax
    jmp     rs_loop
rs_done:
    pop     es
    popa
    clc
    ret

/* ---- data ---- */
mode_list:
    .word 0x144     /* 1024x768x32 */
    .word 0x143     /* 800x600x32  */
    .word 0x142     /* 640x480x32  */
    .word 0xFFFF

msg_stage2:   .asciz "YoroOS stage2\r\n"
msg_novbe:    .asciz "YoroOS: VBE linear framebuffer not available\r\n"
msg_nokernel: .asciz "YoroOS: kernel load failed\r\n"
msg_vbe_ok:   .asciz "VBE mode set\r\n"
msg_load:     .asciz "loading kernel\r\n"
msg_loaded:   .asciz "kernel loaded, jumping to protected mode\r\n"

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
    mov     eax, 0x5000
    /* absolute 32-bit far jump to the kernel entry point 0x10000 */
    .byte   0xEA
    .long   0x00010000
    .word   0x0008
