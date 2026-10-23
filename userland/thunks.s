/* YoroOS - import thunks for Win32 samples.
 *
 * Every imported function gets a 6-byte jump through its IAT slot; the PE
 * loader fills the slot with the address of the YoroOS kernel stub.
 */
.code32
.text

.macro THUNK name
.globl \name
\name:
    jmp *__imp_\name
.endm

/* kernel32.dll - slots must stay contiguous and in this order */
THUNK GetStdHandle
THUNK WriteConsoleA
THUNK WriteFile
THUNK GetTickCount
THUNK Sleep
THUNK GetCommandLineA
THUNK GetModuleHandleA
THUNK GetVersion
THUNK ExitProcess

/* user32.dll */
THUNK MessageBoxA

.section .iat,"aw",@progbits
.macro SLOT name
.globl __imp_\name
__imp_\name:
    .long 0
.endm

/* kernel32.dll */
SLOT GetStdHandle
SLOT WriteConsoleA
SLOT WriteFile
SLOT GetTickCount
SLOT Sleep
SLOT GetCommandLineA
SLOT GetModuleHandleA
SLOT GetVersion
SLOT ExitProcess

/* user32.dll */
SLOT MessageBoxA

.section .note.GNU-stack, "", @progbits
