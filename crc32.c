#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <endian.h>

#include "ware.h"

static char *progname;

static void
usage(void)
{
        fprintf(stderr, "usage: %s <binfile>\n", progname);
        exit(1);
}

static const uint32_t crc_poly = 0x4c11db7;
static const uint32_t initial_crc = 0xffffffff;

uint32_t crc32_calculate(uint32_t crc, void *data, size_t length)
{
	uint32_t *p = data;
	size_t i, b;

	for (i = 0; i < length; i += sizeof(uint32_t)) {
		crc ^= *p++;
		for (b = 0; b < 32; b++) {
			if (crc & (1 << 31))
				crc = (crc << 1) ^ crc_poly;
			else
				crc <<= 1;
		}
	}

	return crc;
}

int main(int argc, char** argv)
{
	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	if (argc < 2)
		usage();

	char *filename = argv[1];

	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: open(%s): %s\n", progname, filename, strerror(errno));
		exit(1);
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "%s: stat(%s): %s\n", progname, filename, strerror(errno));
		exit(1);
	}

	void *data = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (data == (void *)-1) {
		fprintf(stderr, "%s: mmap(%s): %s\n", progname, filename, strerror(errno));
		exit(1);
	}

	close(fd);

	vanmoof_ware_t ware;
	memcpy(&ware, data, sizeof(ware));

	uint32_t crc = initial_crc;

	if (le32toh(ware.magic) == WARE_MAGIC) {
		printf("%s: vanmoof ware magic OK\n", filename);
		printf("%s: vanmoof ware version %08x\n", filename, le32toh(ware.version));
		printf("%s: vanmoof ware CRC 0x%08x\n", filename, le32toh(ware.crc));
		printf("%s: vanmoof ware length 0x%08x\n", filename, le32toh(ware.length));
		printf("%s: vanmoof ware date %s\n", filename, ware.date);
		printf("%s: vanmoof ware time %s\n", filename, ware.time);

		uint32_t length = le32toh(ware.length);
		if (length > st.st_size) {
			printf("%s: vanmoof ware length 0x%08x extends beyond file size 0x%08zx\n",
				filename, length, st.st_size);
			exit(1);
		}

		uint32_t ware_crc = le32toh(ware.crc);

		ware.crc = 0xffffffff;
		ware.length = 0xffffffff;
		crc = crc32_calculate(crc, &ware, sizeof(ware));

		crc = crc32_calculate(crc, data + sizeof(ware), length - sizeof(ware));

		printf("%s: CRC 0x%08x %s\n", filename, crc, crc == ware_crc ? "OK" : "FAIL");

		if (crc != ware_crc)
			exit(1);
	} else {
		printf("%s: vanmoof ware magic not found, assume boot-loader binary\n", filename);

		uint32_t expected_crc = le32toh(*(uint32_t *)(data + st.st_size - sizeof(crc)));
		uint8_t *version = (uint8_t *)(data + st.st_size - 2 * sizeof(crc));

		printf("%s: version %c%c%c\n", filename, version[3], version[2], version[1]);
		printf("%s: expected CRC 0x%08x\n", filename, expected_crc);

		crc = crc32_calculate(crc, data, st.st_size - sizeof(crc));

		printf("%s: CRC 0x%08x %s\n", filename, crc, crc == expected_crc ? "OK" : "FAIL");

		if (crc != expected_crc)
			exit(1);
	}

	return 0;
}
