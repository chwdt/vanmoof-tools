typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

#define NULL ((void *)0)

#define FLASH_START 0x08000000
#define FLASH_SIZE  0x00180000

#define UART7_START 0x40007800
#define WWDG_START  0x40002c00

typedef struct {
	volatile uint32_t CR;
} WWDG_t;

typedef struct {
	volatile uint32_t SR;
	volatile uint32_t DR;
	volatile uint32_t BRR;
	volatile uint32_t CR1;
} UART_t;

typedef uint32_t (*strtoul_t) (const char *, char **, uint32_t base);
typedef void (*help_t) (void);

static void dump_dataline(uint32_t addr, const uint8_t *data);
static void uart_send(const char* data, size_t len);

void
dump(const char *args)
{
	UART_t *UART7 = (void *)UART7_START;
	strtoul_t strtoul = (strtoul_t)(0x3f8c8 + 1);
	help_t help = (help_t)(0x35e04 + 1);
	char *end;
	uint32_t addr;
	uint32_t n;

	addr = strtoul(args, &end, 16);
	if (*end != ' ') {
		help();
		return;
	}

	n = strtoul(end + 1, &end, 16);

	addr &= ~(0xf);
	n = (n + 0xf) & ~(0xf);

	uint32_t cr1 = UART7->CR1;
	UART7->CR1 = cr1 & ~(0x1f0);

	uint8_t *data = (uint8_t *) addr;
	while (n > 0) {
		dump_dataline(addr, data);

		addr += 16;
		data += 16;
		n -= 16;
	}

	while (!(UART7->SR & 0x40))
		/* wait */;
	UART7->CR1 = cr1;
}

static uint8_t byte_to_rec(char* buffer, uint8_t byte)
{
	uint8_t nib = (byte >> 4) & 0x0f;
	if (nib < 10)
		buffer[0] = '0' + nib;
	else 
		buffer[0] = 'A' + (nib - 10);
	nib = byte & 0x0f;
	if (nib < 10)
		buffer[1] = '0' + nib;
	else 
		buffer[1] = 'A' + (nib - 10);
	return byte;
}

static void wdg(void)
{
	WWDG_t *WWDG = (void *)WWDG_START;
	WWDG->CR = 0x7f;
}

static void uart_send(const char* data, size_t len)
{
	UART_t *UART7 = (void *)UART7_START;
	size_t i;

	for (i = 0; i < len; i++) {
		while (!(UART7->SR & 0x80))
			/* wait */;
		UART7->DR = data[i];
	}

	wdg();
}

static void
dump_dataline(uint32_t addr, const uint8_t *data)
{
	char buffer[80];
	char *p = buffer;
	uint32_t i;

	byte_to_rec(p, (addr >> 24) & 0xff); p += 2;
	byte_to_rec(p, (addr >> 16) & 0xff); p += 2;
	byte_to_rec(p, (addr >> 8) & 0xff); p += 2;
	byte_to_rec(p, addr & 0xff); p += 2;
	*p++ = '\t';

	for (i = 0; i < 16; i++) {
		if (i > 0) {
			*p++ = ' ';
			if (i == 8) {
				*p++ = ' ';
				*p++ = ' ';
			}
		}
		byte_to_rec(p, data[i]); p += 2;
	}
	*p++ = '\t';

	for (i = 0; i < 16; i++) {
		if (i == 8)
			*p++ = ' ';
		if (0x1f < data[i] && data[i] < 0x7f)
			*p++ = data[i];
		else
			*p++ = '.';
	}
	*p++ = '\r';
	*p++ = '\n';

	uart_send(buffer, p - buffer);
}
