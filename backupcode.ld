/*
 * backupcode.ld -- link backupcode.c as a mainware-slot image.
 *
 * Layout muco-boot expects (matches the OEM mainware): the 0xAA55AA55
 * envelope at the slot base, the Cortex-M vector table 0x200 in, then code.
 * objcopy -O binary yields a file whose offset 0 == 0x08020000; end is
 * 4-aligned so the post-build MPEG-2 CRC pass (word-at-a-time) is exact.
 */

OUTPUT_FORMAT(elf32-littlearm)
ENTRY(reset_handler)

MEMORY
{
	FLASH (rx) : ORIGIN = 0x08020000, LENGTH = 384K   /* mainware slot */
}

SECTIONS
{
	.envelope 0x08020000 :
	{
		KEEP(*(.envelope))
	} > FLASH

	.isr_vector 0x08020200 :
	{
		. = ALIGN(4);
		KEEP(*(.isr_vector))
	} > FLASH

	.text :
	{
		. = ALIGN(4);
		*(.text)
		*(.text*)
		*(.rodata)
		*(.rodata*)
		. = ALIGN(4);
	} > FLASH

	/DISCARD/ :
	{
		*(.ARM.exidx*)
		*(.ARM.extab*)
		*(.comment)
		*(.note*)
	}
}
