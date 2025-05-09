typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

#define LOGGER (0x6d90 + 1)
#define READ_EXTFLASH (0x1c5a4 + 1)
#define GET_KEY (0x20bb8 + 1)
#define SHOW_HELP (0x21244 + 1)
#define SSCANF (0x23838 + 1)

#define WDT 0x40080000

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

static void strcpy(char *, const char *);
static int strcmp(const char *, const char *);
static int strncmp(const char *, const char *, size_t);

static int usage(const char *name, const char *args);

static int dump_keys(const char *args);
static int dump_mem(const char *args);
static int dump_extflash(const char *args);

int
dump(int what, char *cmdline)
{
	static const char text[] = "dump keys/memory/extflash";
	static const char file[] = __FILE__;
	show_help_t show_help = (show_help_t)SHOW_HELP;

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
