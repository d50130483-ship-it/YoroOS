.code16
.intel_syntax noprefix
.global _start

_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov ah, 0x02
    mov al, 15
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov bx, 0x1000
    mov es, bx
    xor bx, bx
    int 0x13

    mov ax, 0x0013
    int 0x10

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    ljmp 0x08, offset pm_entry

.align 8
gdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_desc:
    .word 23
    .long gdt

.code32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    call 0x10000

    hlt
1:  jmp 1b

.org 510
.word 0xAA55
