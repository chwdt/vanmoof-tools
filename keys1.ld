OUTPUT_FORMAT(elf32-littlearm)

ENTRY(_start)

INCLUDE rtos_ram.ld

MEMORY
{
        free_rom        : ORIGIN = 0x2c67c, LENGTH = 0x1000
}

SECTIONS
{
        .text :
        {
                *(.text)
                *(.rodata*)
        } > free_rom = 0xff
}
