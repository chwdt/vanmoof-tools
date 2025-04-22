#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
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

	int fd = open(filename, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s: open(%s): %s\n", progname, filename, strerror(errno));
		exit(1);
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "%s: stat(%s): %s\n", progname, filename, strerror(errno));
		exit(1);
	}

	void *data = mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
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

		switch (le32toh(ware.version)) {
		case 0x010903f4:
			if ((ware_crc == 0x76c1ab9d) && (length == 0x0002fcc8)) {

				/* Patch cycling of power level 5 not allowed */
				uint32_t offset = 0x08027fb6 - MAINWARE_OFFSET;
				uint16_t* pPowerButton = data + offset;

				/* Patch power level 5 not allowed from BLE */
				offset = 0x0803a7ca - MAINWARE_OFFSET;
				uint16_t* pPowerBLE = data + offset;

				/* Patch region 3 not allowed from BLE */
				offset = 0x0803a876 - MAINWARE_OFFSET;
				uint16_t* pRegionBLE = data + offset;

				/* Patch region 3 reset to region 1 during boot */
				offset = 0x0803ef00 - MAINWARE_OFFSET;
				uint16_t* pRegion = data + offset;

				/* Patch power level 5 reset to 4 during boot */
				offset = 0x0803ef0a - MAINWARE_OFFSET;
				uint16_t* pPower = data + offset;

				if ((pRegion[0] == 0xbf08) &&				/* it eq */
				    (pRegion[1] == 0xf884) && (pRegion[2] == 0x9109) &&	/* strb.eq.w r9,[r4,#0x109] */
				    (pRegionBLE[0] == 0x2b02) &&			/* cmp r3,#2 */
				    (pPower[0] == 0x2b04) &&				/* cmp r3,#4 */
				    (pPowerBLE[0] == 0x2b04) &&				/* cmp r3,#4 */
				    (pPowerButton[0] == 0x2b04)) {			/* cmp r3,#4 */

					pRegion[0] = 0xbf00;				/* nop */
					pRegion[1] = 0xbf00;				/* nop */
					pRegion[2] = 0xbf00;				/* nop */
					pRegionBLE[0] = 0x2b03;				/* cmp r3,#3 */

					pPower[0] = 0x2b05;				/* cmp r3,#5 */
					pPowerBLE[0] = 0x2b05;				/* cmp r3,#5 */
					pPowerButton[0] = 0x2b05;			/* cmp r3,#5 */

					uint32_t crc = initial_crc;

					ware.crc = 0xffffffff;
					ware.length = 0xffffffff;

					memset(ware.date, 0xff, sizeof(ware.date));
					memset(ware.time, 0xff, sizeof(ware.time));

					time_t t = time(0);
					struct tm *tm = gmtime(&t);

					strftime(ware.date, sizeof(ware.date), "%b %e %Y", tm);
					strftime(ware.time, sizeof(ware.time), "%T", tm);

					crc = crc32_calculate(crc, &ware, sizeof(ware));

					crc = crc32_calculate(crc, data + sizeof(ware), length - sizeof(ware));

					ware.crc = htole32(crc);
					ware.length = htole32(length);

					memcpy(data, &ware, sizeof(ware));
				} else {
					fprintf(stderr, "%s: Code to patch does not match original\n", filename);
					exit(1);
				}
			} else {
				fprintf(stderr, "%s: CRC or length do not match original\n", filename);
				exit(1);
			}
			break;

		default:
			fprintf(stderr, "%s: No patch for this version available, yet\n", progname);
			exit(1);
		}
	} else {
		fprintf(stderr, "%s: Not a vanmoof ware file\n", filename);
		exit(1);
	}

	munmap(data, st.st_size);

	return 0;
}
