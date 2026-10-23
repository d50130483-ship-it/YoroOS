/* YoroOS kernel entry (32-bit protected mode, flat segments, no paging) */
.code32
.intel_syntax noprefix
.section .text.entry, "ax"
.globl _start
.extern kmain
.extern _bss_start
.extern _bss_end

_start:
    cli
    mov     dword ptr [boot_info], eax
    mov     dx, 0x3F8
    mov     al, 'K'
    out     dx, al
    mov     esp, 0x90000
    xor     ebp, ebp

    /* zero .bss */
    mov     edi, offset _bss_start
    mov     ecx, offset _bss_end
    sub     ecx, edi
    xor     eax, eax
    rep     stosb

    call    kmain
hang:
    cli
    hlt
    jmp     hang

.section .data
.globl boot_info
boot_info:
    .long 0
