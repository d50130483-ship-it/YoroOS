/* Layout for YoroOS' Win32 sample programs.
 *
 * The image is linked at the address the loader maps it to (4 MiB), so no
 * base relocations are needed and the code can use plain absolute addresses.
 * .iat holds one 4-byte slot per imported function: the loader writes the
 * resolved stub address there, the thunks in thunks.s jump through them.
 */
ENTRY(_start)
SECTIONS
{
    . = 0x00401000;
    _image_start = .;

    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    _text_end = .;

    .iat : {
        _iat_start = .;
        *(.iat)
        _iat_end = .;
    }
    _image_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
