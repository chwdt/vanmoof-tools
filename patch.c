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

typedef struct {
	off_t offset;
	size_t size;
	const uint16_t *expect;
	const uint16_t *patch;
} patch_t;

static const uint16_t exp_cmp_r3_2[] = {
	0x2b02		/*	cmp	r3, #2			*/
};

static const uint16_t rpl_cmp_r3_3[] = {
	0x2b03		/*	cmp	r3, #3			*/
};

static const uint16_t exp_cmp_r3_4[] = {
	0x2b04		/*	cmp	r3, #4			*/
};

static const uint16_t rpl_cmp_r3_5[] = {
	0x2b05		/*	cmp	r3, #5			*/
};

static const uint16_t exp_region_1_9_1[] = {
	0xbf08,		/*	it	eq			*/
	0xf884, 0x9109	/*	strb.eq.w r9, [r4,#0x109]	*/
};

static const uint16_t rpl_region_1_9_1[] = {
	0xbf00,		/*	nop				*/
	0xbf00,		/*	nop				*/
	0xbf00		/*	nop				*/
};

static const uint16_t exp_power_button_1_9_1[] = {
	0x3301,		/*	adds	r3, #1			*/
	0xb2db,		/*	uxtb	r3, r3			*/
	0x2b04,		/*	cmp	r3, #4			*/
	0xbf88,		/*	it	hi			*/
	0x2300,		/*	mov.hi	r3, #0			*/
};

static const uint16_t rpl_power_button_1_9_1[] = {
	0x4901,		/*	ldr	r1, [pc,#4]		*/
	0x4788,		/*	blx	r1			*/
	0xe001,		/*	b.n	<pc+4>			*/
	0x0029, 0x0802	/*	addr	0x08020028+1		*/
};

static const uint16_t exp_power_level_inc[] = {
	0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff
};

static const uint16_t rpl_power_level_inc[] = {
	0x3301,		/*	adds	r3, #1			*/
	0xf897, 0x2109,	/*	ldrb.w	r2, [r7,#0x109]		*/
	0x2a03,		/*	cmp	r2, #3			*/
	0xd101,		/*	bne.n	<pInc+0xc>		*/
	0x2b05,		/*	cmp	r3, #5			*/
	0xe000,		/*	b.n	<pInc+0xe>		*/
	0x2b04,		/*	cmp	r3, #4			*/
	0xbf88,		/*	it	hi			*/
	0x2300,		/*	mov.hi	r3, #0			*/
	0x4770		/*	bx	lr			*/
};

#define N_ARRAY(a) (sizeof(a) / sizeof(a[0]))

/* Patch region 3 not allowed from BLE */
static const patch_t patch_region_ble_1_9_1 = {
	0x0803a876,
	N_ARRAY(rpl_cmp_r3_3),
	exp_cmp_r3_2,
	rpl_cmp_r3_3,
};

/* Patch power level 5 not allowed from BLE */
static const patch_t patch_power_ble_1_9_1 = {
	0x0803a7ca,
	N_ARRAY(rpl_cmp_r3_5),
	exp_cmp_r3_4,
	rpl_cmp_r3_5,
};

/* Patch power level 5 reset to 4 during boot */
static const patch_t patch_power_1_9_1 = {
	0x0803ef0a,
	N_ARRAY(rpl_cmp_r3_5),
	exp_cmp_r3_4,
	rpl_cmp_r3_5,
};

/* Patch region 3 reset to region 1 during boot */
static const patch_t patch_region_1_9_1 = {
	0x0803ef00,
	N_ARRAY(rpl_region_1_9_1),
	exp_region_1_9_1,
	rpl_region_1_9_1
};

/* Patch cycling of power level 5 not allowed */
static const patch_t patch_power_button_1_9_1 = {
	0x08027fb2,
	N_ARRAY(rpl_power_button_1_9_1),
	exp_power_button_1_9_1,
	rpl_power_button_1_9_1
};

/* Increment function for power level cycling patch */
static const patch_t patch_power_level_inc = {
	0x08020028,
	N_ARRAY(rpl_power_level_inc),
	exp_power_level_inc,
	rpl_power_level_inc
};

static const patch_t *patches_1_9_1[] = {
	&patch_region_ble_1_9_1,
	&patch_power_ble_1_9_1,
	&patch_power_1_9_1,
	&patch_region_1_9_1,
	&patch_power_button_1_9_1,
	&patch_power_level_inc
};

typedef struct {
	const char *date;
	const char *time;
	size_t n_patches;
	const patch_t **patches;
} patchset_t;

static const patchset_t patchset_1_9_1 = {
	"Apr 23 2025",
	"09:38:07",
	N_ARRAY(patches_1_9_1),
	patches_1_9_1,
};

static const uint32_t crc_poly = 0x4c11db7;
static const uint32_t initial_crc = 0xffffffff;

static uint32_t crc32_calculate(uint32_t crc, const void *data, size_t length)
{
	const uint32_t *p = data;
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

static uint32_t ware_crc(uint32_t crc, const vanmoof_ware_t *ware, const void *data, size_t length)
{
	vanmoof_ware_t tmp;

	memcpy(&tmp, ware, sizeof(tmp));
	tmp.crc = 0xffffffff;
	tmp.length = 0xffffffff;

	crc = crc32_calculate(crc, &tmp, sizeof(tmp));

	crc = crc32_calculate(crc, data + sizeof(tmp), length - sizeof(tmp));

	return crc;
}

static int verify_expected(const char *filename, const void *data, const patchset_t *set)
{
	int expect_ok = 1;

	for (size_t i = 0; i < set->n_patches; i++) {
		const patch_t* p = set->patches[i];

		uint32_t offset = p->offset - MAINWARE_OFFSET;
		const uint16_t* inst = data + offset;

		for (size_t j = 0; j < p->size; j++) {
			if (inst[j] != p->expect[j]) {
				fprintf(stderr, "%s: patch @0x%08x: inst[%zu] 0x%04x != expected 0x%04x\n",
					filename, p->offset, i, inst[i], p->expect[i]);
				expect_ok = 0;
			}
		}
	}

	return expect_ok;
}

static void apply_patches(void *data, const patchset_t *set)
{
	for (size_t i = 0; i < set->n_patches; i++) {
		const patch_t* p = set->patches[i];

		uint32_t offset = p->offset - MAINWARE_OFFSET;
		uint16_t* inst = data + offset;

		for (size_t j = 0; j < p->size; j++) {
			inst[j] = p->patch[j];
		}
	}
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

		uint32_t crc = ware_crc(initial_crc, &ware, data, length);

		printf("%s: CRC 0x%08x %s\n", filename, crc, crc == le32toh(ware.crc) ? "OK" : "FAIL");

		if (crc != le32toh(ware.crc))
			exit(1);

		switch (le32toh(ware.version)) {
		case 0x010903f4:
			if ((le32toh(ware.crc) == 0x76c1ab9d) && (length == 0x0002fcc8)) {
				if (verify_expected(filename, data, &patchset_1_9_1)) {
					apply_patches(data, &patchset_1_9_1);

					memset(ware.date, 0xff, sizeof(ware.date));
					memset(ware.time, 0xff, sizeof(ware.time));

					strcpy(ware.date, patchset_1_9_1.date);
					strcpy(ware.time, patchset_1_9_1.time);

					crc = ware_crc(initial_crc, &ware, data, length);

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
