/* YoroOS — вход в ядро: стек, обнуление BSS, вызов kmain */
    .text
    .globl _start
_start:
    cli
    /* стек — над BSS с запасом 4 КБ, выровнен на 16
       (0x90000 попадал внутрь BSS: обнуление и данные kmain затирали стек) */
    movl    $(__bss_end + 4104), %eax
    andl    $-16, %eax
    movl    %eax, %esp
    xor     %ebp, %ebp
    movl    $__bss_start, %edi
    movl    $__bss_end, %ecx
    subl    %edi, %ecx
    shrl    $2, %ecx
    xorl    %eax, %eax
    cld
    rep     stosl
    call    kmain
1:
    hlt
    jmp     1b
