/* YoroOS 0.2 MBR / El Torito boot sector (512 bytes at 0x7C00).
 * Probes stage2 ("YS2!") across:
 *   1) GPT+MBR hybrid disk (stage2 LBA 36, kernel LBA 40)
 *   2) Raw image / MBR disk (stage2 LBA 4, kernel LBA 128)
 *   3) El Torito No-Emulation CD (2048B sectors, stage2 ISO LBA 23)
 *   4) CHS fallback geometries (18/2, 18/1, 63/16)
 * Keeps bytes 0x1B8..0x1FD 100% standard MBR Disk Signature + 4 Partitions!
 */
.code16
.intel_syntax noprefix
.section .text, "ax"
.globl _boot_start

.equ DRIVE,   0x600
.equ EXT,     0x601
.equ MODE,    0x602
.equ GPTMODE, 0x603
.equ LBA,     0x604
.equ SPT,     0x610
.equ HEADS,   0x612
.equ DAP,     0x620
.equ P,       0x540

_boot_start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    sti

    mov     [DRIVE], dl
    mov     word ptr [SPT], ax
    mov     byte ptr [GPTMODE], al

    /* 1a) GPT hybrid disk: LBA 36 */
    mov     word ptr [EXT], 0x0001
    mov     word ptr [LBA], 36
    call    probe_stage2
    jc      1f
    inc     byte ptr [GPTMODE]
    jmp     .Lfound
1:
    /* 1b) Raw image: LBA 4 */
    mov     word ptr [EXT], 0x0001
    mov     word ptr [LBA], 4
    call    probe_stage2
    jnc     .Lfound

    /* 2) CD No-Emulation: ISO LBA 23 */
    mov     word ptr [EXT], 0x0101
    mov     word ptr [LBA], 23
    call    probe_stage2
    jnc     .Lfound

    /* 3) CHS geometries (LBA 4 fits on cylinder 0 head 0 track 1 for all!) */
    mov     word ptr [EXT], 0
    mov     word ptr [SPT], 18
    mov     word ptr [HEADS], 2
    mov     word ptr [LBA], 4
    call    probe_stage2
    jnc     .Lfound
    mov     word ptr [HEADS], 1
    call    probe_stage2
    jnc     .Lfound
    mov     word ptr [SPT], 63
    mov     word ptr [HEADS], 16
    call    probe_stage2
    jnc     .Lfound

    mov     si, offset msg_fail + 0x7C00
.Lp1:
    lodsb
    test    al, al
    jz      .Lhalt
    mov     ah, 0x0E
    mov     bx, 7
    int     0x10
    jmp     .Lp1
.Lhalt:
    cli
    hlt
    jmp     .Lhalt

.Lfound:
    mov     ax, 9
    mov     cx, 4
    mov     dword ptr [P + 4], 4
    mov     dword ptr [P + 16], 128
    cmp     byte ptr [GPTMODE], 1
    jne     2f
    mov     dword ptr [P + 4], 36
    mov     dword ptr [P + 16], 40
    jmp     3f
2:
    cmp     byte ptr [MODE], 1
    jne     3f
    mov     ax, 11
    xor     cx, cx
    mov     dword ptr [P + 4], 23
    mov     dword ptr [P + 16], 54
3:
    mov     [P], ax
    movzx   ax, byte ptr [EXT]
    mov     [P + 2], ax
    mov     ax, [SPT]
    mov     [P + 8], ax
    mov     ax, [HEADS]
    mov     [P + 10], ax
    mov     [P + 12], cx
    .byte   0xEA
    .word   0x8000, 0x0000

probe_stage2:
    pusha
    push    es
    cmp     byte ptr [EXT], 0
    je      .Lchs

    mov     word ptr [DAP], 0x0010
    mov     word ptr [DAP + 2], 1
    mov     dword ptr [DAP + 4], 0x08000000     /* 0800:0000 = linear 0x8000 */
    movzx   eax, word ptr [LBA]
    mov     [DAP + 8], eax
    xor     eax, eax
    mov     [DAP + 12], eax
    mov     si, DAP
    mov     dl, [DRIVE]
    mov     ah, 0x42
    int     0x13
    jnc     .Lrok
    mov     byte ptr [EXT], 0
    xor     ax, ax
    mov     dl, [DRIVE]
    int     0x13
    jmp     .Lrfail

.Lchs:
    mov     bx, [SPT]
    test    bx, bx
    jz      .Lrfail
    mov     ax, [LBA]
    xor     dx, dx
    div     bx
    inc     dx
    mov     cx, dx                              /* cl = 1-based sector */
    mov     bx, [HEADS]
    xor     dx, dx
    div     bx
    mov     ch, al                              /* cylinder low */
    mov     dh, dl                              /* head */
    mov     dl, [DRIVE]
    mov     bx, 0x0800
    mov     es, bx
    xor     bx, bx
    mov     ax, 0x0201
    int     0x13
    jc      .Lrfail

.Lrok:
    cmp     dword ptr [0x8000], 0x21325359
    jne     .Lrfail
    pop     es
    popa
    clc
    ret
.Lrfail:
    pop     es
    popa
    stc
    ret

msg_fail: .asciz "Boot err"

.org 0x1B0
.ascii "YBT2"
.long 0                      /* 0x1B4: u32 kernel sector count (512B units) */
.org 0x1FE
.word 0xAA55
