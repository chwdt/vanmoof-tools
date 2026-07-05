/*
 * backupcode.c -- one-shot mainware-slot payload that writes the owner
 * "backup code" (the three-digit handlebar unlock combination) straight into
 * the smart controller's internal config flash.
 *
 * Purpose: recovery for a bike whose BLE chip has died. The backup code is
 * normally programmed over Bluetooth (CMD_BLE_SECURITY_BACKUP_CODE); with a
 * dead BLE chip there is no way to (re)set it, so a locked bike can no longer
 * be unlocked at the handlebar. This payload sets a known code directly.
 *
 * This is NOT a protection bypass. Uploading it requires opening the bike and
 * driving the smart controller's serial bootloader -- the same physical access
 * that lets you read the BLE unlock key out of the SPI flash and unlock the
 * bike that way. It only exists for the case where the BLE chip is gone.
 *
 * ---------------------------------------------------------------------------
 * How it runs
 *   muco-boot (the STM32 first-stage loader at 0x08000000) validates the
 *   mainware slot with an MPEG-2 CRC-32 over the image only -- no signature.
 *   So a self-built image with a valid VanMoof envelope + CRC boots as
 *   "mainware". This file *is* that image: a 0x28-byte 0xAA55AA55 envelope at
 *   the slot base, the real Cortex-M vector table 0x200 in, then the code.
 *   Finalise the envelope crc/length after objcopy (see the Makefile /
 *   patch_image_header.py); muco-boot then loads SP + reset vector and jumps.
 *
 * Internal flash map (STM32F413VGT6, from the mainware decomp):
 *   0x08000000  muco-boot            0x08008000  bike config bank A (sector 2)
 *   0x0800C000  bike config bank B   0x08010000  shifterware
 *   0x08020000  mainware  <- us      0x08060000  shadow
 * The two config banks are their own 16 KB sectors, independent of the
 * mainware slot: overwriting mainware with this payload does not touch them,
 * and re-flashing the real mainware afterwards does not erase them. The code
 * we write therefore survives the round-trip.
 *
 * What it does
 *   Read config bank A (fall back to bank B) as the 0xD0-byte config record,
 *   verify its self-checking MPEG-2 CRC-32 -- and compare the two redundant
 *   banks against each other, warning if they disagree or one is invalid --
 *   then patch ONLY the backup_code half-word, recompute the record CRC, and
 *   rewrite both banks (which also heals the divergence). Every other
 *   setting (region, model/e-shifter/display bits, hw revision, volumes,
 *   assist curves, dark threshold) is preserved byte-for-byte. It then prints
 *   the result on muco-boot's console UART (USART1, 115200) and halts;
 *   press ESC into the bootloader and re-upload the real mainware -- the new
 *   code is already stored and the real firmware picks it up on next boot.
 *
 * Config record layout (0x34 words = 0xD0 bytes, mirrors the OEM
 * config_persist_dual_bank record; ctx offsets in the decomp / README):
 *   w[0]  ctx+0xF4  sound group low       w[1]  ctx+0xF8  sound group medium
 *   w[2]  ctx+0xFC  sound group high
 *   w[3]  ctx+0x100 (dark_lux << 16) | backup_code   <- only the low half set
 *   w[4..0x32]      volumes / region / model / assist tables / ...
 *   w[0x33] ctx+0x1C0  MPEG-2 CRC-32 over w[0..0x32]; a fresh CRC over all
 *                      0x34 words then reads 0 (the loader's integrity check).
 *
 * Choose the code at build time (plain decimal 0..999, the number you enter
 * at the handlebar): `make backupcode.bin BACKUP_CODE=123`.
 *
 * Caveats: not yet verified on hardware. Assumes a bike that currently boots
 * (so bank A or B holds a valid record) -- if both banks fail CRC it writes
 * nothing. Assumes muco-boot leaves flash lockable/unlockable (we KEY-unlock
 * ourselves). The window watchdog is not serviced; the whole operation is
 * well under a second, and the independent watchdog is reloaded across it.
 */

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

/* The unlock code to store: the decimal handlebar combination, 0..999
 * (stored value == d0 + d1*10 + d2*100, i.e. just the number). */
#ifndef BACKUP_CODE
#define BACKUP_CODE 123
#endif
#if (BACKUP_CODE) < 0 || (BACKUP_CODE) > 999
#error "BACKUP_CODE must be a decimal 0..999"
#endif
#if (BACKUP_CODE) == 255
#warning "BACKUP_CODE 255 (0x00FF) is the firmware's 'code not set' sentinel"
#endif

/* --- config banks (their own 16 KB sectors on the STM32F413) --------------- */
#define CONFIG_BANK_A   0x08008000u   /* flash sector 2 */
#define CONFIG_BANK_B   0x0800C000u   /* flash sector 3 */
#define CONFIG_SECTOR_A 2u
#define CONFIG_SECTOR_B 3u
#define RECORD_WORDS    0x34u         /* 0xD0 bytes: 0x33 data words + 1 CRC word */

/* --- STM32F4 embedded-flash controller (RM0430) ---------------------------- */
#define FLASH_KEYR (*(volatile uint32_t *)0x40023C04u)
#define FLASH_SR   (*(volatile uint32_t *)0x40023C0Cu)
#define FLASH_CR   (*(volatile uint32_t *)0x40023C10u)
#define FLASH_KEY1 0x45670123u         /* CR unlock sequence, word 1 */
#define FLASH_KEY2 0xCDEF89ABu         /* CR unlock sequence, word 2 */
#define FLASH_SR_BSY   (1u << 16)
#define FLASH_SR_ERR   0xF3u           /* EOP|OPERR|WRPERR|PGAERR|PGPERR|PGSERR */
#define FLASH_CR_PG    (1u << 0)
#define FLASH_CR_SER   (1u << 1)
#define FLASH_CR_SNB   (0x1Fu << 3)    /* sector-number field */
#define FLASH_CR_STRT  (1u << 16)
#define FLASH_CR_PSIZE_X32 (0x2u << 8) /* 32-bit programming; needs Vdd >= 2.7 V */
#define FLASH_CR_LOCK  (1u << 31)

/* independent watchdog: reloading with 0xAAAA is a no-op when it is not
 * running, so it is safe to poke unconditionally while we work. */
#define IWDG_KR (*(volatile uint32_t *)0x40003000u)
#define IWDG_RELOAD 0xAAAAu

/* system control block: AIRCR system-reset request (unused; we halt instead). */
#define SCB_AIRCR (*(volatile uint32_t *)0xE000ED0Cu)

/* USART1 -- muco-boot's console/YModem UART (the only USART its image
 * references). It is already clocked and configured (115200 8N1) when the
 * bootloader hands off to us, so we just push bytes; we never change the baud,
 * so output matches whatever terminal the operator uploaded from. */
#define USART1_SR  (*(volatile uint32_t *)0x40011000u)
#define USART1_DR  (*(volatile uint32_t *)0x40011004u)
#define USART1_CR1 (*(volatile uint32_t *)0x4001100Cu)
#define USART_SR_TXE (1u << 7)
#define USART_CR1_UE (1u << 13)
#define USART_CR1_TE (1u << 3)

/*
 * MPEG-2 CRC-32 (poly 0x04C11DB7, init 0xFFFFFFFF, no reflection, over
 * little-endian 32-bit words) -- exactly the STM32 hardware CRC unit the
 * firmware uses, and the same algorithm as vanmoof-tools crc32.c
 * (crc32_calculate). Storing crc(0xFFFFFFFF, w, 0x33) in w[0x33] makes a fresh
 * CRC over all 0x34 words read 0, which is the loader's record check.
 */
static uint32_t crc32_mpeg2(uint32_t crc, const volatile uint32_t *p, uint32_t words)
{
	uint32_t i, b;

	for (i = 0; i < words; i++) {
		crc ^= p[i];
		for (b = 0; b < 32; b++) {
			if (crc & 0x80000000u)
				crc = (crc << 1) ^ 0x04C11DB7u;
			else
				crc <<= 1;
		}
	}
	return crc;
}

static void flash_wait(void)
{
	while (FLASH_SR & FLASH_SR_BSY)
		/* busy */;
}

static void flash_unlock(void)
{
	if (FLASH_CR & FLASH_CR_LOCK) {
		FLASH_KEYR = FLASH_KEY1;
		FLASH_KEYR = FLASH_KEY2;
	}
}

static void flash_erase_sector(uint32_t sector)
{
	IWDG_KR = IWDG_RELOAD;
	flash_wait();
	FLASH_SR  = FLASH_SR_ERR;                       /* clear stale errors (w1c) */
	FLASH_CR  = (FLASH_CR & ~FLASH_CR_SNB) | FLASH_CR_PSIZE_X32;
	FLASH_CR |= FLASH_CR_SER | (sector << 3);       /* select sector to erase */
	FLASH_CR |= FLASH_CR_STRT;                       /* start */
	flash_wait();
	FLASH_CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
	IWDG_KR = IWDG_RELOAD;
}

static void flash_program_words(uint32_t addr, const uint32_t *w, uint32_t words)
{
	uint32_t i;

	flash_wait();
	FLASH_SR  = FLASH_SR_ERR;
	FLASH_CR  = (FLASH_CR & ~0x300u) | FLASH_CR_PSIZE_X32;
	FLASH_CR |= FLASH_CR_PG;
	for (i = 0; i < words; i++) {
		*(volatile uint32_t *)(addr + i * 4) = w[i];   /* the store programs the word */
		flash_wait();
	}
	FLASH_CR &= ~FLASH_CR_PG;
	IWDG_KR = IWDG_RELOAD;
}

/* A config bank is valid iff the MPEG-2 CRC over all 0x34 words is 0. */
static int bank_valid(uint32_t bank)
{
	return crc32_mpeg2(0xFFFFFFFFu, (const volatile uint32_t *)bank, RECORD_WORDS) == 0;
}

/* The dual banks are meant to be identical copies -- return 1 if any of the
 * 0x34 record words differ between them. */
static int banks_differ(uint32_t a, uint32_t b)
{
	const volatile uint32_t *pa = (const volatile uint32_t *)a;
	const volatile uint32_t *pb = (const volatile uint32_t *)b;
	uint32_t i;

	for (i = 0; i < RECORD_WORDS; i++)
		if (pa[i] != pb[i])
			return 1;
	return 0;
}

/* --- status output on muco-boot's console UART ---------------------------- */

static void uart_putc(char c)
{
	/* Bounded TXE wait: on the live bootloader UART this spins only a few
	 * thousand cycles per byte; if the UART were somehow disabled it gives
	 * up instead of hanging the halt. */
	uint32_t guard = 0x100000u;

	while (!(USART1_SR & USART_SR_TXE) && guard)
		guard--;
	USART1_DR = (uint8_t)c;
}

static void uart_puts(const char *s)
{
	while (*s)
		uart_putc(*s++);
}

static void uart_put_u16(uint16_t v)
{
	char buf[6];
	int  n = 0;

	do {
		buf[n++] = (char)('0' + (v % 10u));
		v /= 10u;
	} while (v);
	while (n)
		uart_putc(buf[--n]);
}

void reset_handler(void)
{
	uint32_t rec[RECORD_WORDS];
	const volatile uint32_t *src;
	int a_ok, b_ok, differ;
	uint32_t i;

	/* muco-boot's vector table is still active; keep IRQs off across the
	 * flash operation so a stray interrupt can't vector into the loader. */
	__asm__ volatile ("cpsid i" ::: "memory");

	/* Capture the pre-write bank state: each bank's validity, and whether the
	 * two copies (which are supposed to be identical) disagree. */
	a_ok   = bank_valid(CONFIG_BANK_A);
	b_ok   = bank_valid(CONFIG_BANK_B);
	differ = banks_differ(CONFIG_BANK_A, CONFIG_BANK_B);

	if (a_ok)
		src = (const volatile uint32_t *)CONFIG_BANK_A;
	else if (b_ok)
		src = (const volatile uint32_t *)CONFIG_BANK_B;
	else
		src = 0;   /* no trustworthy record to preserve -- do nothing */

	if (src) {
		for (i = 0; i < RECORD_WORDS; i++)
			rec[i] = src[i];

		/* w[3] = (dark_threshold_lux << 16) | backup_code; touch only the
		 * low half so the auto-light threshold is preserved. */
		rec[3] = (rec[3] & 0xFFFF0000u) | ((uint32_t)(BACKUP_CODE) & 0xFFFFu);

		/* refresh the record's self-checking CRC over w[0..0x32]. */
		rec[RECORD_WORDS - 1] = crc32_mpeg2(0xFFFFFFFFu, rec, RECORD_WORDS - 1);

		flash_unlock();
		flash_erase_sector(CONFIG_SECTOR_A);
		flash_program_words(CONFIG_BANK_A, rec, RECORD_WORDS);
		flash_erase_sector(CONFIG_SECTOR_B);
		flash_program_words(CONFIG_BANK_B, rec, RECORD_WORDS);
	}

	/* Report the outcome on muco-boot's console UART (still up from the
	 * bootloader session the operator used to upload us). */
	USART1_CR1 |= USART_CR1_UE | USART_CR1_TE;   /* idempotent: ensure TX on */
	uart_puts("\r\n");
	if (src) {
		/* Surface an unhealthy pre-existing config: the banks are redundant
		 * copies, so a mismatch (or one invalid bank) means they were already
		 * inconsistent. We base on bank A (the bank the firmware trusts first)
		 * and rewrite both, which also restores consistency. */
		if (a_ok && b_ok && differ)
			uart_puts("backupcode: WARNING config banks A and B differed "
				  "-- based on bank A\r\n");
		else if (!a_ok || !b_ok)
			uart_puts("backupcode: one config bank was invalid "
				  "-- healed from the other\r\n");
		uart_puts("backupcode: backup code ");
		uart_put_u16((uint16_t)(BACKUP_CODE));
		uart_puts(" set in config flash (banks A+B)\r\n");
	} else {
		uart_puts("backupcode: no valid config record in either bank "
			  "-- nothing written\r\n");
	}
	uart_puts("backupcode: done -- CPU halted. Press ESC to re-enter the "
		  "bootloader, then re-upload the real mainware.\r\n");

	/* Halt (kicking the independent watchdog) rather than resetting: this
	 * image still occupies the mainware slot, so a reset would just re-run
	 * us. The operator re-enters the bootloader and re-uploads real mainware;
	 * the code is already stored in config flash. */
	for (;;)
		IWDG_KR = IWDG_RELOAD;
}

static void hang(void)
{
	for (;;)
		/* trap */;
}

/* --- image envelope + vector table ---------------------------------------- */

/* VanMoof application "ware" envelope at the slot base (vanmoof_ware_t, see
 * ware.h). magic + version are fixed here; crc and length are stamped after
 * objcopy (left as 0xFFFFFFFF, which is also how the CRC pass blanks them). */
__attribute__((section(".envelope"), used))
static const struct {
	uint32_t magic;
	uint8_t  version[4];
	uint32_t crc;
	uint32_t length;
	char     date[12];
	char     time[12];
} envelope = {
	0xAA55AA55u,             /* WARE_MAGIC */
	{ 0xF4, 0x01, 0x00, 0x00 }, /* type MAIN (0xF4), version 0.0.1 */
	0xFFFFFFFFu,             /* crc    -- patched post-build */
	0xFFFFFFFFu,             /* length -- patched post-build */
	__DATE__,
	__TIME__,
};

/* Cortex-M vector table, placed by the linker at slot+0x200 where muco-boot
 * expects it. Only the first two slots matter to the loader (SP + reset). */
typedef void (*vector_t)(void);
__attribute__((section(".isr_vector"), used))
static const vector_t vectors[] = {
	(vector_t)0x20037000u,   /* initial SP (OEM mainware value) */
	reset_handler,           /* reset */
	hang,                    /* NMI            */
	hang,                    /* HardFault      */
	hang,                    /* MemManage      */
	hang,                    /* BusFault       */
	hang,                    /* UsageFault     */
	0, 0, 0, 0,              /* reserved       */
	hang,                    /* SVCall         */
	hang,                    /* DebugMonitor   */
	0,                       /* reserved       */
	hang,                    /* PendSV         */
	hang,                    /* SysTick        */
};
