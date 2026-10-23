/* YoroOS - trampoline that runs a PE image on its own stack.
 *
 * pe_enter(entry, stack_top) saves the kernel callee-saved registers,
 * switches to the PE stack and calls the entry point.  Win32 code returns
 * either by falling off the entry point (we land on pe_epilogue) or by
 * calling ExitProcess, whose stub jumps to pe_exit_now.
 */
.code32
.text

.globl pe_enter
.type  pe_enter, @function
pe_enter:                       /* (u32 entry, u32 stack_top) */
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  %esp, pe_return_esp
    movl  20(%esp), %ecx        /* entry */
    movl  24(%esp), %eax        /* top of the PE stack */
    andl  $-16, %eax
    subl  $64, %eax
    movl  %eax, %esp
    xorl  %ebp, %ebp
    call  *%ecx

.globl pe_epilogue
pe_epilogue:
    movl  pe_return_esp, %esp
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret

.globl pe_exit_now
pe_exit_now:                    /* called from the ExitProcess stub */
    jmp   pe_epilogue

.section .bss
.align 4
.globl pe_return_esp
pe_return_esp:
    .long 0

.section .note.GNU-stack, "", @progbits
