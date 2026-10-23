/* YoroOS 0.2 - Native PE32 (x86) & PE32+ (x86_64 ms_abi) Execution Trampoline */

#ifdef __x86_64__
.code64
.text
.globl pe_enter
.globl pe_exit_now

/* void pe_enter(uptr entry [rcx], uptr stack_top [rdx]) — Microsoft x64 ABI */
pe_enter:
    pushq %rbp
    pushq %rbx
    pushq %rdi
    pushq %rsi
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq  %rsp, pe_return_rsp(%rip)
    andq  $-16, %rdx
    subq  $64, %rdx
    movq  %rdx, %rsp
    xorq  %rbp, %rbp
    call  *%rcx
pe_epilogue64:
    movq  pe_return_rsp(%rip), %rsp
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %rsi
    popq  %rdi
    popq  %rbx
    popq  %rbp
    ret

pe_exit_now:
    jmp   pe_epilogue64

.bss
.align 8
pe_return_rsp:
    .quad 0

#else
.code32
.text
.globl pe_enter
.globl pe_exit_now

/* void pe_enter(uptr entry [esp+4], uptr stack_top [esp+8]) — x86 cdecl */
pe_enter:
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  %esp, pe_return_esp
    movl  20(%esp), %ecx
    movl  24(%esp), %eax
    andl  $-16, %eax
    subl  $64, %eax
    movl  %eax, %esp
    xorl  %ebp, %ebp
    call  *%ecx
pe_epilogue32:
    movl  pe_return_esp, %esp
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret

pe_exit_now:
    jmp   pe_epilogue32

.section .bss
.align 4
pe_return_esp:
    .long 0
#endif
