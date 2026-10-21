/* YoroOS — загрузочный сектор (LBA 0). Загружает loader (LBA 1..8) в 0x0600 и передаёт ему управление. */
    .code16
    .text
    .globl _start
_start:
    xor     %ax, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     $0x7C00, %sp
    sti
    mov     %dl, bootdrv

    /* Читаем 8 секторов (LBA 1..8) в 0000:0600 через int 13h AH=42h (DAП) */
    movw    $1, dap_lba
    movw    $8, dap_count
    movw    $0x0600, dap_seg
    mov     $dap, %si
    mov     bootdrv, %dl
    mov     $0x42, %ah
    int     $0x13
    jc      err

    ljmp    $0, $0x0600

err:
    movb    $'E', %al
    movb    $0x0E, %ah
    movw    $0x0007, %bx
    int     $0x10
halt1:
    hlt
    jmp     halt1

bootdrv:
    .byte   0

    .align  2
dap:
    .byte   16, 0
dap_count:
    .word   0
dap_off:
    .word   0
dap_seg:
    .word   0
dap_lba:
    .quad   0

    .fill   510-(.-_start), 1, 0
    .word   0xAA55
