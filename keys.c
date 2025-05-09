typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

typedef void (*logger_t) (const char* file, int lineno, const char* function, uint32_t flags, const char* fmt, ...);
typedef int (*load_key_t) (unsigned int index, uint8_t *data);
typedef void (*show_help_t) (const char*, const char*);

static void strcpy(char *, const char *);
static int strcmp(const char *, const char *);

int
dump_keys(int what, char *help)
{
	static const char fmt[] = "Key 0x%02x: %s %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x %08x %08x CRC %08x";
	static const char name[] = "dump-keys";
	static const char text[] = "dump API keys";
	static const char file[] = __FILE__;
	show_help_t show_help = (show_help_t)(0x21244 + 1);
	logger_t logger = (logger_t)(0x6d90 + 1);
	load_key_t get_key = (load_key_t)(0x20bb8 + 1);
	uint8_t key_data[32];
	unsigned int i;

	switch (what) {
	case 0:
		show_help(name, text);
		return 0;

	case 1:
		strcpy(help, name);
		return 0;

	case 2:
		if (strcmp(help, name) != 0) {
			return 2;
		}

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

	default:
		return 1;
	}
}

static void strcpy(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	*d = '\0';
}

static int strcmp(const char *s1, const char *s2)
{
	while (*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}
	return *s1 - *s2;
}
