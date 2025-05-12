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

static int send_srec(uint8_t type, uint32_t addr, size_t addr_len, const uint8_t *data, size_t len);

void dump_flash(void)
{
	UART_t *UART7 = (void *)UART7_START;
	uint8_t *FLASH = (void *)FLASH_START;
	uint32_t head = 0x48445200;
	size_t count = 0;
	size_t i, j;

	uint32_t cr1 = UART7->CR1;
	UART7->CR1 = cr1 & ~(0x1f0);

	send_srec(0, 0, 2, (uint8_t *)&head, 4);
	for (i = 0; i < FLASH_SIZE; i += 32) {
		send_srec(3, FLASH_START + i, 4, FLASH + i, 32);
		count++;
	}
	send_srec(5, count, 2, NULL, 0);
	send_srec(7, FLASH_START, 4, NULL, 0);

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

static int send_srec(uint8_t type, uint32_t addr, size_t addr_len, const uint8_t *data, size_t len)
{
	char buffer[2 + 2 + 8 + 64 + 2 + 2];
	char* p = buffer + 2;
	uint8_t total;
	uint8_t sum = 0;
	size_t i;

	buffer[0] = 'S';
	buffer[1] = '0' + type;
	total = addr_len + len + 1;
	sum += byte_to_rec(p, total);
	p += 2;
	switch (addr_len) {
		case 4:
			sum += byte_to_rec(p, (addr >> 24) & 0xff);
			p += 2;
		case 3:
			sum += byte_to_rec(p, (addr >> 16) & 0xff);
			p += 2;
		default:
			sum += byte_to_rec(p, (addr >> 8) & 0xff);
			p += 2;
			sum += byte_to_rec(p, addr & 0xff);
			p += 2;
	}
	for (i = 0; i < len; i++) {
		sum += byte_to_rec(p, data[i]);
		p += 2;
	}
	sum ^= 0xff;
	byte_to_rec(p, sum);
	p += 2;
	*p++ = '\r';
	*p++ = '\n';

	uart_send(buffer, p - buffer);
}
