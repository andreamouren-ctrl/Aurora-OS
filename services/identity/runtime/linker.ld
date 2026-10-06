OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x0000000000400000;

    .text : ALIGN(16)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    }

    /*
     * The current Aurora Ring 3 loader consumes a flat binary and therefore
     * knows only the file image size, not a separate ELF memsz for NOBITS
     * sections. Materialize zero-initialized static storage into the flat
     * image so every address used by the runtime is mapped by map_image().
     */
    .data : ALIGN(16)
    {
        *(.data .data.*)
        . = ALIGN(16);
        BYTE(0)
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.llvm_addrsig)
    }
}
