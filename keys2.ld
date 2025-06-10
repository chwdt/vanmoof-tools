OUTPUT_FORMAT(elf32-littlearm)

ENTRY(_start)

MEMORY
{
        free_rom        : ORIGIN = 0x3567c, LENGTH = 0x1000
}

SECTIONS
{
        .text :
        {
                *(.text)
                *(.rodata)
        } > free_rom = 0xff
}
