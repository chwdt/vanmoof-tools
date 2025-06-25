typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

#ifdef VERSION_1_4_1
#define LOGGER (0x6d90 + 1)
#define READ_EXTFLASH (0x1c5a4 + 1)
#define GET_KEY (0x20bb8 + 1)
#define SHOW_HELP (0x21244 + 1)
#define SSCANF (0x23838 + 1)
#define SYSTEM_PUTCHAR (0x260f8 + 1)
#endif

#ifdef VERSION_2_4_1
#define LOGGER (0x7714 + 1)
#define READ_EXTFLASH (0x21640 + 1)
#define GET_KEY (0x26ea2 + 1)
#define SHOW_HELP (0x27744 + 1)
#define SSCANF (0x2a670 + 1)
#define SYSTEM_PUTCHAR (0x2dbc8 + 1)
#endif

#define WDT 0x40080000
#define JTAGCFG 0x40090034

typedef struct {
	__volatile__ uint32_t LOAD;
	__volatile__ uint32_t VALUE;
	__volatile__ uint32_t CTL;
	__volatile__ uint32_t ICR;
	__volatile__ uint32_t RIS;
	__volatile__ uint32_t MIS;
	uint32_t unused0[0x100];
	__volatile__ uint32_t TEST;
	__volatile__ uint32_t INT_CAUS;
	uint32_t unused1[0x1f8];
	__volatile__ uint32_t LOCK;
} WDT_t;

typedef void (*logger_t) (const char* file, int lineno, const char* function, uint32_t flags, const char* fmt, ...);
typedef int (*get_key_t) (unsigned int index, uint8_t *data);
typedef int (*read_extflash_t) (uint32_t addr, size_t len, uint8_t *data);
typedef void (*show_help_t) (const char*, const char*);
typedef int (*sscanf_t) (const char *str, const char *fmt, ...);
typedef void (*system_putchar_t) (char c);

static void strcpy(char *, const char *);
static int strcmp(const char *, const char *);
static int strncmp(const char *, const char *, size_t);

static int usage(const char *name, const char *args);

static int dump_keys(const char *args);
static int dump_mem(const char *args);
static int dump_extflash(const char *args);

static int patch_ble_boot(const char *args);

int
dump(int what, char *cmdline)
{
	static const char text[] = "dump keys/memory/extflash";
	static const char file[] = __FILE__;
	show_help_t show_help = (show_help_t)SHOW_HELP;
	logger_t logger = (logger_t)LOGGER;

	switch (what) {
	case 0:
		show_help("dump", text);
		return 0;

	case 1:
		strcpy(cmdline, "dump");
		return 0;

	case 2:
		if (strncmp(cmdline, "dump", 4) != 0)
			return 2;

		if (cmdline[4] == ' ') {
			char *args = cmdline + 5;

			if (strcmp(args, "keys") == 0)
				return dump_keys(args);

			if (strncmp(args, "mem", 3) == 0)
				return dump_mem(args);

			if (strncmp(args, "extflash", 8) == 0)
				return dump_extflash(args);

			if (strncmp(args, "ccfg", 4) == 0)
				return patch_ble_boot(args);
		}

		return usage("dump", "<keys|mem|extflash>");

	default:
		return 1;
	}
}

static int
usage(const char *name, const char *args)
{
	logger_t logger = (logger_t)LOGGER;

	logger(__FILE__, __LINE__, name, 9, "usage: %s %s", name, args);
	return 2;
}

static void
wdg(void)
{
	WDT_t *pWDT = (WDT_t *)WDT;

	while (pWDT->LOCK == 1) {
		pWDT->LOCK = 0x1acce551;
	}
	pWDT->ICR = 0;
	pWDT->LOCK = 0;
}

static int
dump_keys(const char* args)
{
	static const char fmt[] = "Key 0x%02x: %s %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x %08x %08x CRC %08x";
	get_key_t get_key = (get_key_t)GET_KEY;
	logger_t logger = (logger_t)LOGGER;
	uint8_t key_data[32];
	unsigned int i;

	for (i = 0; i < 0x80; i++) {
		if (get_key(i, key_data)) {
			uint32_t *p0 = (uint32_t*)(key_data + 16);
			uint32_t *p1 = (uint32_t*)(key_data + 20);
			uint32_t *crc = (uint32_t*)(key_data + 28);
			char type[5];
			type[0] = key_data[24];
			type[1] = key_data[25];
			type[2] = key_data[26];
			type[3] = key_data[27];
			type[4] = '\0';
			logger(__FILE__, __LINE__, __FUNCTION__, 9, fmt, i, type,
				key_data[0], key_data[1], key_data[2], key_data[3],
				key_data[4], key_data[5], key_data[6], key_data[7],
				key_data[8], key_data[9], key_data[10], key_data[11],
				key_data[12], key_data[13], key_data[14], key_data[15],
				*p0, *p1, *crc);
		}
	}

	return 0;
}

static void
dump_dataline(uint32_t addr, const uint8_t *data)
{
	static const char fmt[] = "%08x\t%02x %02x %02x %02x %02x %02x %02x %02x   %02x %02x %02x %02x %02x %02x %02x %02x\t%s";
	logger_t logger = (logger_t)LOGGER;
	uint32_t i, j;

	char text[16 + 2];
	for (i = j = 0; i < 16; i++, j++) {
		if (i == 8)
			text[j++] = ' ';
		if (0x1f < data[i] && data[i] < 0x7f)
			text[j] = data[i];
		else
			text[j] = '.';
	}
	text[j] = '\0';

	logger(__FILE__, __LINE__, __FUNCTION__, 9, fmt, addr,
		data[0], data[1], data[2], data[3],
		data[4], data[5], data[6], data[7],
		data[8], data[9], data[10], data[11],
		data[12], data[13], data[14], data[15],
		text);
}

static int
dump_mem(const char *args)
{
	sscanf_t sscanf = (sscanf_t)SSCANF;
	uint32_t addr;
	uint32_t n;

	if (sscanf(args, "mem %x %x", &addr, &n) != 2) {
		return usage("dump mem", "<addr> <n>");
	}

	addr &= ~(0xf);
	n = (n + 0xf) & ~(0xf);

	uint8_t *data = (uint8_t *) addr;
	while (n > 0) {
		dump_dataline(addr, data);

		wdg();

		addr += 16;
		data += 16;
		n -= 16;
	}

	return 0;
}

static int
dump_extflash(const char *args)
{
	read_extflash_t read_extflash = (read_extflash_t)READ_EXTFLASH;
	sscanf_t sscanf = (sscanf_t)SSCANF;
	uint8_t data[16];
	uint32_t addr;
	uint32_t n;

	if (sscanf(args, "extflash %x %x", &addr, &n) != 2) {
		return usage("dump extflash", "<addr> <n>");
	}

	addr &= ~(0xf);
	n = (n + 0xf) & ~(0xf);

	while (n > 0) {
		read_extflash(addr, 16, data);
		dump_dataline(addr, data);

		wdg();

		addr += 16;
		n -= 16;
	}

	return 0;
}

#define ROM_API_TABLE		((uint32_t *) 0x10000180)

#define ROM_API_FLASH_TABLE	((uint32_t*) (ROM_API_TABLE[10]))
#define ROM_API_UART_TABLE	((uint32_t*) (ROM_API_TABLE[20]))
#define ROM_API_VIMS_TABLE	((uint32_t*) (ROM_API_TABLE[22]))

#define ROM_FlashSectorErase \
    ((uint32_t (*)(uint32_t ui32SectorAddress)) \
    ROM_API_FLASH_TABLE[5])

#define ROM_FlashProgram \
    ((uint32_t (*)(uint8_t *pui8DataBuffer, uint32_t ui32Address, uint32_t ui32Count)) \
    ROM_API_FLASH_TABLE[6])

#define FLASH_FCFG_B0_SSIZE0 0x40032430

#define UART1 0x4000b000

struct UART {
	volatile uint32_t DR;
	volatile uint32_t RSR_ECR;
	uint32_t unused0[4];
	volatile uint32_t FR;
	uint32_t unused1[2];
	volatile uint32_t IBRD;
	volatile uint32_t FBRD;
	volatile uint32_t LCRH;
	volatile uint32_t CTL;
	volatile uint32_t IFLS;
	volatile uint32_t IMSC;
	volatile uint32_t RIS;
	volatile uint32_t MIS;
	volatile uint32_t ICR;
	volatile uint32_t DMACTL;
};

#define ROM_VIMSModeSet \
    ((void (*)(uint32_t ui32Base, uint32_t ui32Mode)) \
    ROM_API_VIMS_TABLE[1])

#define ROM_VIMSModeGet \
    ((uint32_t (*)(uint32_t ui32Base)) \
    ROM_API_VIMS_TABLE[2])

#define VIMS 0x40034000

static uint32_t vims_enable(void)
{
	uint32_t mode = ROM_VIMSModeGet(VIMS) & 0xff;
	if (mode != 0) {
		ROM_VIMSModeSet(VIMS, 0);
		while (ROM_VIMSModeGet(VIMS) != 0)
			/* wait */;
	}
	return mode;
}

static uint32_t vims_mode_set(uint32_t mode)
{
	if (mode != 0) {
		ROM_VIMSModeSet(VIMS, 1);
	}
}

static int flash_sector_erase(uint32_t address)
{
	uint32_t mode = vims_enable();
	int res = ROM_FlashSectorErase(address);
	vims_mode_set(mode);
	return res;
}

static int flash_program(uint8_t *data, uint32_t address, size_t len)
{
	uint32_t mode = vims_enable();
	int res = ROM_FlashProgram(data, address, len);
	vims_mode_set(mode);
	return res;
}

#define CCFG_BASE 0x57f00
#define CCFG_BIM_DATE 0x3c
#define CCFG_BIM_TIME 0x48
#define CCFG_TI_OPTIONS 0xe0
#define CCFG_TAP_DAP_0 0xe4
#define CCFG_TAP_DAP_1 0xe8

static void *memcpy(void *dst, const void *src, size_t len);

static int patch_ble_boot(const char* args)
{
	uint32_t *pTIOptions = (uint32_t *)(CCFG_BASE + CCFG_TI_OPTIONS);
	uint32_t *pTapDap0 = (uint32_t *)(CCFG_BASE + CCFG_TAP_DAP_0);
	uint32_t *pTapDap1 = (uint32_t *)(CCFG_BASE + CCFG_TAP_DAP_1);
	uint32_t *jtagcfg = (uint32_t *)JTAGCFG;
	logger_t logger = (logger_t)LOGGER;
	uint32_t sector_size;
	uint32_t last_sector = 0x56000;
	uint32_t tmp_dst = 0x46000;
	uint8_t data[256];
	uint32_t src_addr;
	uint32_t dst_addr;
	uint32_t total;

	logger(__FILE__, __LINE__, __FUNCTION__, 9, "CCFG_TI_OPTIONS: 0x%08x", *pTIOptions);
	logger(__FILE__, __LINE__, __FUNCTION__, 9, "CCFG_TAP_DAP_0:  0x%08x", *pTapDap0);
	logger(__FILE__, __LINE__, __FUNCTION__, 9, "CCFG_TAP_DAP_1:  0x%08x", *pTapDap1);
	logger(__FILE__, __LINE__, __FUNCTION__, 9, "JTAGCFG:         0x%08x", *jtagcfg);

	if ((*pTIOptions == 0xffffffc5) && (*pTapDap0 == 0xffc5c5c5) && (*pTapDap1 == 0xffc5c5c5)) {
		return 0;
	}

	sector_size = (*(uint32_t *)FLASH_FCFG_B0_SSIZE0 & 0x0f) << 10;
	logger(__FILE__, __LINE__, __FUNCTION__, 9, "Patch CCFG ... sector_size: %u", sector_size);

	src_addr = last_sector;
	dst_addr = tmp_dst;
	total = sector_size;

	flash_sector_erase(dst_addr);

	while (total) {
		memcpy(data, (void*)src_addr, sizeof(data));

		if (total == sizeof(data)) {
			*(uint32_t *)(data + CCFG_TI_OPTIONS) = 0xffffffc5;
			*(uint32_t *)(data + CCFG_TAP_DAP_0) = 0xffc5c5c5;
			*(uint32_t *)(data + CCFG_TAP_DAP_1) = 0xffc5c5c5;
			strcpy(data + CCFG_BIM_DATE, "Jun 16 2025");
			strcpy(data + CCFG_BIM_TIME, "13:12:42");
		}

		flash_program(data, dst_addr, sizeof(data));
		src_addr += sizeof(data);
		dst_addr += sizeof(data);
		total -= sizeof(data);
	}

	src_addr = tmp_dst;
	dst_addr = last_sector;
	total = sector_size;

	flash_sector_erase(dst_addr);

	while (total) {
		flash_program((uint8_t*)src_addr, dst_addr, sizeof(data));
		src_addr += sizeof(data);
		dst_addr += sizeof(data);
		total -= sizeof(data);
	}

	return 0;
}

static void *
memcpy(void *dst, const void *src, size_t len)
{
	uint8_t *d = dst;
	const uint8_t *s = src;
	while (len--)
		*d++ = *s++;
	return dst;
}

static void
strcpy(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	*d = '\0';
}

static int
strcmp(const char *s1, const char *s2)
{
	while (*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}
	return *s1 - *s2;
}

static int
strncmp(const char *s1, const char *s2, size_t n)
{
	while (*s1 && (*s1 == *s2) && --n) {
		s1++;
		s2++;
	}
	return *s1 - *s2;
}

extern void *xdcRomStatePtr;

typedef uint16_t xdc_Bool;
typedef xdc_Bool __T1_ti_sysbios_family_arm_m3_Hwi_Module_State__excActive;
typedef xdc_Bool *ARRAY1_ti_sysbios_family_arm_m3_Hwi_Module_State__excActive;
typedef ARRAY1_ti_sysbios_family_arm_m3_Hwi_Module_State__excActive __TA_ti_sysbios_family_arm_m3_Hwi_Module_State__excActive;

typedef struct Hwi_Module_State {
	char *xdcTaskSP;
	__TA_ti_sysbios_family_arm_m3_Hwi_Module_State__excActive excActive;
} ti_sysbios_family_arm_m3_Hwi_Module_State;

#define ti_sysbios_family_arm_m3_Hwi_Module__state__V_offset 0xd0
#define Hwi_module ((ti_sysbios_family_arm_m3_Hwi_Module_State *)(xdcRomStatePtr + ti_sysbios_family_arm_m3_Hwi_Module__state__V_offset))

void
System_putchar(uint8_t c)
{
	if (Hwi_module->excActive[0]) {
		struct UART *uart = (struct UART *)UART1;
		uint32_t imsc = uart->IMSC;
		uart->IMSC = 0;
		uart->CTL |= 0x100;
		while (uart->FR & 0x20)
			/* wait */;
		if (c == '\n') {
			uart->DR = '\r';
			while (uart->FR & 0x20)
				/* wait */;
		}
		uart->DR = c;
		uart->IMSC = imsc;
	} else {
		system_putchar_t putc = (system_putchar_t)SYSTEM_PUTCHAR;
		putc(c);
	}
}
