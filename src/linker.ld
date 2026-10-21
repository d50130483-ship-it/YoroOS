OUTPUT_FORMAT("binary")
ENTRY(_start)
SECTIONS
{
    . = 0x10000;
    .text : {
        build/kernel_entry.o(.text)
        *(.text)
        *(.rodata*)
    }
    .data : { *(.data) }
    .bss : { *(.bss COMMON) }
}
