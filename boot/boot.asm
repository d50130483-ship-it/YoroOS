/* YoroOS boot sector - 512 bytes loaded by the BIOS at 0x7C00.
 *
 * Loads the first sector of stage2 and jumps to it; stage2 then pulls in the
 * rest of itself.  Works for El Torito "no emulation" CDs (2048-byte sectors,
 * image at ISO LBA 22), floppy emulation / real floppies and installed disks.
 *
 * stage2 starts at image sector 4 (byte 2048) so it is aligned for both
 * sector sizes: LBA 4 on a 512-byte device, ISO LBA 23 on a CD.  Its first
 * four bytes are the magic "YS2!" used here to verify reads and to find the
 * CHS geometry of BIOSes without int13h extensions.
 */
.code16
.intel_syntax noprefix
.section .text, "ax"
.globl _boot_start

.equ DRIVE,   0x600
.equ EXT,     0x601          /* 1 = int13h AH=42h works */
.equ MODE,    0x602          /* 0 = 512-byte sectors, 1 = 2048-byte CD */
.equ RETRY,   0x603
.equ LBA,     0x604          /* word */
.equ DEST,    0x608          /* word */
.equ SPT,     0x610
.equ HEADS,   0x612
.equ TMP,     0x614
.equ DAP,     0x620
.equ SCRATCH, 0x3000
.equ P,       0x540          /* boot parameters handed to stage2 */

_boot_start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    sti

    mov     [DRIVE], dl
    mov     word ptr [SPT], 0

    mov     si, offset msg_boot + 0x7C00
    call    print

    /* EXT and MODE are adjacent, so one 16-bit write sets both. */

    /* 1) plain 512-byte device with int13h extensions (HDD, USB, disk) */
    mov     word ptr [EXT], 0x0001
    mov     word ptr [LBA], 4
    call    probe_stage2
    jnc     .Lfound

    /* 2) El Torito "no emulation" CD: 2048-byte sectors, stage2 at ISO LBA 23 */
    mov     word ptr [EXT], 0x0101             /* EXT = 1, MODE = 1 */
    mov     word ptr [LBA], 23
    call    probe_stage2
    jnc     .Lfound

    /* 3) floppy emulation: LBA 4 lives in the first track for every
     *    geometry, so 18/2 (1.44M) always finds stage2.  Clearing the
     *    EXT/MODE pair also resets the CD sector size set by attempt 2. */
    mov     word ptr [EXT], 0
    mov     word ptr [SPT], 18
    mov     word ptr [HEADS], 2
    mov     word ptr [LBA], 4
    call    probe_stage2
    jnc     .Lfound

    /* 4) single-sided 1.44M El Torito emulation, then hard-disk geometry */
    mov     word ptr [HEADS], 1
    call    probe_stage2
    jnc     .Lfound
    mov     word ptr [SPT], 63
    mov     word ptr [HEADS], 16
    call    probe_stage2
    jnc     .Lfound

.Lfail:
    mov     si, offset msg_fail + 0x7C00
    call    print
    cli
.Lhalt:
    hlt
    jmp     .Lhalt

.Lfound:
    /* ---- parameters for stage2 ---- */
    mov     ax, 9
    mov     cx, 4
    mov     dword ptr [P + 4], 4                   /* stage2 LBA */
    mov     dword ptr [P + 16], 128                /* kernel LBA */
    cmp     byte ptr [MODE], 1
    jne     .Lsave
    mov     ax, 11
    xor     cx, cx
    mov     dword ptr [P + 4], 23
    mov     dword ptr [P + 16], 54
.Lsave:
    mov     [P], ax                                /* sector shift */
    movzx   ax, byte ptr [EXT]
    mov     [P + 2], ax
    mov     ax, [SPT]
    mov     [P + 8], ax
    mov     ax, [HEADS]
    mov     [P + 10], ax
    mov     [P + 12], cx                           /* stage2 sectors */

    .byte   0xEA
    .word   0x8000
    .word   0x0000

/* read the first stage2 sector to 0x8000 and verify the magic */
probe_stage2:
    mov     word ptr [DEST], 0x8000
    call    read_one
    jc      .Lps_bad
    cmp     dword ptr [0x8000], 0x21325359
    jne     .Lps_bad
    clc
    ret
.Lps_bad:
    stc
    ret

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

/* read one sector from [LBA] to [DEST]; uses LBA when EXT=1, else CHS */
read_one:
    pusha
    push    es
    cmp     byte ptr [EXT], 0
    je      .Lchs

    mov     byte ptr [DAP], 16
    mov     byte ptr [DAP + 1], 0
    mov     word ptr [DAP + 2], 1
    mov     ax, [DEST]
    mov     bx, ax
    and     bx, 0x0F
    mov     [DAP + 4], bx
    shr     ax, 4
    mov     [DAP + 6], ax
    mov     ax, [LBA]
    mov     [DAP + 8], ax
    mov     word ptr [DAP + 10], 0
    mov     word ptr [DAP + 12], 0
    mov     si, DAP
    mov     dl, [DRIVE]
    mov     ah, 0x42
    int     0x13
    jnc     .Lrok
    mov     byte ptr [EXT], 0
    xor     ax, ax                  /* reset the drive after a failed LBA call */
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
    mov     [TMP], dx                              /* sector, 1-based */
    mov     bx, [HEADS]
    test    bx, bx
    jz      .Lrfail
    xor     dx, dx
    div     bx
    mov     [TMP + 2], dl                          /* head */
    mov     [TMP + 4], ax                          /* cylinder */

    mov     ax, [DEST]
    mov     bx, ax
    and     bx, 0x0F
    mov     [TMP + 6], bx
    shr     ax, 4
    mov     es, ax

    mov     bx, [TMP + 6]
    mov     ax, [TMP + 4]
    mov     ch, al
    mov     cl, [TMP]
    mov     dh, [TMP + 2]
    mov     dl, [DRIVE]
    mov     ax, 0x0201
    int     0x13
    jnc     .Lrok
    jmp     .Lrfail

.Lrok:
    pop     es
    popa
    clc
    ret
.Lrfail:
    pop     es
    popa
    stc
    ret

msg_boot: .asciz "YoroOS\n"
msg_fail: .asciz "Boot err"

/* kernel sector count (512-byte units), patched by tools/mkimage.py */
.org 0x1F0
.word 0
.org 0x1F8
.byte 'Y', 'B', 'T', '0'
.org 0x1FE
.word 0xAA55
