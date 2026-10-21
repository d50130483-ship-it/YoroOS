/* YoroOS — второй-stage загрузчик (LBA 1..8, грузится на 0x0600).
   Выбирает VBE-режим 800x600x32 (или 640x480x32), включает A20,
   загружает ядро (LBA 9..608 -> 0x10000) и уходит в защищённый режим. */
    .code16
    .text
    .globl _start
_start:
    mov     %dl, ldr_bootdrv

    /* ---- VBE: сканируем режимы, ищем 32bpp LFB ---- */
    xor     %bx, %bx            /* best mode */
    movw    $0, best640
scan:
    mov     %bx, %cx
    cmpw    $0x200, %bx
    jae     scan_done
    movw    $0x0A00, %di        /* буфер ModeInfo, es=0 */
    mov     $0x4F01, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     next_mode
    /* атрибуты: bit0 supported, bit4 graphics, bit7 LFB */
    movw    0x0A00, %ax
    andw    $0x0091, %ax
    cmpw    $0x0091, %ax
    jne     next_mode
    cmpb    $32, 0x0A00+25      /* bpp */
    jne     next_mode
    cmpw    $800, 0x0A00+18
    jne     not_800
    cmpw    $600, 0x0A00+20
    jne     not_800
    mov     %bx, %cx            /* идеал: 800x600x32 */
    jmp     scan_found
not_800:
    cmpw    $640, 0x0A00+18
    jne     next_mode
    cmpw    $480, 0x0A00+20
    jne     next_mode
    mov     %bx, best640
next_mode:
    inc     %bx
    jmp     scan
scan_found:
    jmp     scan_done
scan_done:
    mov     %cx, %bx
    test    %bx, %bx
    jnz     have_mode
    mov     best640, %bx
    test    %bx, %bx
    jz      fail2
have_mode:
    /* перечитываем инфо выбранного режима */
    mov     %bx, %cx
    movw    $0x0A00, %di
    mov     $0x4F01, %ax
    int     $0x10
    /* сохраняем параметры для ядра в 0x0B00: pitch,w,h,bpp,fb */
    movw    0x0A00+16, %ax
    movw    %ax, 0x0B00
    movw    0x0A00+18, %ax
    movw    %ax, 0x0B02
    movw    0x0A00+20, %ax
    movw    %ax, 0x0B04
    movb    0x0A00+25, %al
    movb    %al, 0x0B06
    movl    0x0A00+40, %eax
    movl    %eax, 0x0B08
    /* включаем режим с LFB */
    orw     $0x4000, %bx
    mov     $0x4F02, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     fail2

    /* ---- грузим ядро: 600 секторов с LBA 9 в 0x10000 ---- */
    movw    $0x1000, cur_seg
    movl    $9, cur_lba32
    movw    $9, chunks          /* 9 кусков по 64 сектора */
big_loop:
    movw    $64, dap2_count
    call    read_chunk
    addw    $0x800, cur_seg     /* 64*512/16 = 0x800 параграфов */
    decl    chunks
    jnz     big_loop
    movw    $24, dap2_count     /* + 24 сектора: всего 300 КБ */
    call    read_chunk

    /* ---- A20 ---- */
    inb     $0x92, %al
    orb     $2, %al
    outb    %al, $0x92

    /* ---- защищённый режим ---- */
    cli
    lgdt    gdt_desc
    mov     %cr0, %eax
    or      $1, %eax
    mov     %eax, %cr0
    ljmp    $0x08, $pm32

fail2:
    movb    $'2', %al
    movb    $0x0E, %ah
    movw    $0x0007, %bx
    int     $0x10
halt2:
    hlt
    jmp     halt2

/* read_chunk: читает dap2_count секторов с cur_lba32 в cur_seg:0000 */
read_chunk:
    mov     cur_seg, %ax
    mov     %ax, dap2_seg
    mov     cur_lba32, %eax
    mov     %eax, dap2_lba
    mov     $dap2, %si
    mov     ldr_bootdrv, %dl
    mov     $0x42, %ah
    int     $0x13
    jc      fail2
    mov     dap2_count, %eax
    addl    %eax, cur_lba32
    ret

ldr_bootdrv:
    .byte   0
best640:
    .word   0
cur_seg:
    .word   0
chunks:
    .word   0
cur_lba32:
    .long   0

    .align  2
dap2:
    .byte   16, 0
dap2_count:
    .word   0
dap2_off:
    .word   0
dap2_seg:
    .word   0
dap2_lba:
    .quad   0

    .align  4
gdt:
    .quad   0
    .quad   0x00CF9A000000FFFF  /* код: base 0, limit 4G */
    .quad   0x00CF92000000FFFF  /* данные */
gdt_desc:
    .word   23
    .long   gdt

    .code32
pm32:
    mov     $0x10, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %ax, %ss
    mov     $0x90000, %esp
    movl    $0x10000, %eax
    jmp     *%eax
1:
    hlt
    jmp     1b
