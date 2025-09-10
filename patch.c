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
	fprintf(stderr, "usage: %s [-v] [-f <fake-version>] [-m <model>] <binfile>\n", progname);
	fprintf(stderr, "\nfake-version:\t<major>.<minor>.<patch>\n");
	fprintf(stderr, "model:\t\t<model (3|4)>,<shifter (0|1)>,<display (0|1)>\n");
	exit(1);
}

typedef struct {
	const char* name;
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

static const uint16_t exp_region_1_9_3[] = {
	0xbf08,		/*	it	eq			*/
	0xf884, 0x9109	/*	strb.eq.w r9, [r4,#0x109]	*/
};

static const uint16_t rpl_region_1_9_3[] = {
	0xbf00,		/*	nop				*/
	0xbf00,		/*	nop				*/
	0xbf00		/*	nop				*/
};

static const uint16_t exp_power_button_1_9_3[] = {
	0x3301,		/*	adds	r3, #1			*/
	0xb2db,		/*	uxtb	r3, r3			*/
	0x2b04,		/*	cmp	r3, #4			*/
	0xbf88,		/*	it	hi			*/
	0x2300,		/*	mov.hi	r3, #0			*/
};

static const uint16_t rpl_power_button_1_9_3[] = {
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

#ifdef DUMP

static const uint16_t exp_dump[] = {
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0060 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0070 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0080 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0090 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 00a0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 00b0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 00c0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 00d0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 00e0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 00f0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0100 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0110 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0120 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0130 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0140 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0150 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0160 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0170 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0180 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 0190 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 01a0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 01b0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 01c0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 01d0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 01e0 */
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, /* 01f0 */
};

static const uint16_t rpl_dump_1_9_3[] = {
#include "dump.hex"
};

static const uint16_t exp_help_1_9_3[] = {
	0x5e05, 0x0803
};

static const uint16_t rpl_help_1_9_3[] = {
	0x0061, 0x0802
};

static const uint16_t exp_help_text_1_9_3[] = {
	0x5400, 0x6968, 0x2073, 0x6574, 0x736b, 0x0074	/* "\0This tekst\0" */
};

static const uint16_t rpl_help_text_1_9_3[] = {
	0x4800, 0x6c65, 0x2f70, 0x7564, 0x706d, 0x0000	/* "\0Help/dump\0\0" */
};


#endif /* DUMP */

static const uint16_t exp_version_1_9_3[] = {
	0x03f4, 0x0109
};

static uint16_t rpl_version_any[] = {
	0x00f4, 0x0000
};

static uint16_t exp_model_06[] = {
	0x2306		/*	movs	r3, #6			*/
};

static uint16_t rpl_model_any[] = {
	0x2300		/*	movs	r3, #0			*/
};

#define N_ARRAY(a) (sizeof(a) / sizeof(a[0]))

/* Patch region 3 not allowed from BLE */
static const patch_t patch_region_ble_1_9_3 = {
	"Region 3 from BLE",
	0x0803a876,
	N_ARRAY(rpl_cmp_r3_3),
	exp_cmp_r3_2,
	rpl_cmp_r3_3,
};

/* Patch power level 5 not allowed from BLE */
static const patch_t patch_power_ble_1_9_3 = {
	"Power 5 from BLE",
	0x0803a7ca,
	N_ARRAY(rpl_cmp_r3_5),
	exp_cmp_r3_4,
	rpl_cmp_r3_5,
};

/* Patch power level 5 reset to 4 during boot */
static const patch_t patch_power_1_9_3 = {
	"Power 5 at startup",
	0x0803ef0a,
	N_ARRAY(rpl_cmp_r3_5),
	exp_cmp_r3_4,
	rpl_cmp_r3_5,
};

/* Patch region 3 reset to region 1 during boot */
static const patch_t patch_region_1_9_3 = {
	"Region 3 at startup",
	0x0803ef00,
	N_ARRAY(rpl_region_1_9_3),
	exp_region_1_9_3,
	rpl_region_1_9_3
};

/* Patch cycling of power level 5 not allowed */
static const patch_t patch_power_button_1_9_3 = {
	"Power 5 handle bar",
	0x08027fb2,
	N_ARRAY(rpl_power_button_1_9_3),
	exp_power_button_1_9_3,
	rpl_power_button_1_9_3
};

/* Increment function for power level cycling patch */
static const patch_t patch_power_level_inc = {
	"Power 5 handle bar helper",
	0x08020028,
	N_ARRAY(rpl_power_level_inc),
	exp_power_level_inc,
	rpl_power_level_inc
};

#ifdef DUMP

static const patch_t patch_dump_1_9_3 = {
	"Dump FLASH function",
	0x08020060,
	N_ARRAY(rpl_dump_1_9_3),
	exp_dump,
	rpl_dump_1_9_3
};

static const patch_t patch_help_1_9_3 = {
	"Hijack help command",
	0x0804e05c,
	N_ARRAY(rpl_help_1_9_3),
	exp_help_1_9_3,
	rpl_help_1_9_3
};

static const patch_t patch_help_text_1_9_3 = {
	"Hijack help text",
	0x0804dbb0,
	N_ARRAY(rpl_help_text_1_9_3),
	exp_help_text_1_9_3,
	rpl_help_text_1_9_3
};

#endif /* DUMP */

static const patch_t patch_version_head_1_9_3 = {
	"Fixup header version",
	0x08020004,
	N_ARRAY(rpl_version_any),
	exp_version_1_9_3,
	rpl_version_any
};

static const patch_t patch_version_1_9_3 = {
	"Fixup mainware version",
	0x0803f0f0,
	N_ARRAY(rpl_version_any),
	exp_version_1_9_3,
	rpl_version_any
};

static const patch_t patch_model0_1_9_3 = {
	"Patch bike model: 0",
	0x0803ebd8,
	N_ARRAY(rpl_model_any),
	exp_model_06,
	rpl_model_any
};

static const patch_t patch_model1_1_9_3 = {
	"Patch bike model: 1",
	0x0803ef3c,
	N_ARRAY(rpl_model_any),
	exp_model_06,
	rpl_model_any
};

static const patch_t *patches_1_9_3[] = {
	&patch_region_ble_1_9_3,
	&patch_power_ble_1_9_3,
	&patch_power_1_9_3,
	&patch_region_1_9_3,
	&patch_power_button_1_9_3,
	&patch_power_level_inc,
#ifdef DUMP
	&patch_help_1_9_3,
	&patch_help_text_1_9_3,
	&patch_dump_1_9_3,
#endif /* DUMP */
};

static const patch_t *version_1_9_3[] = {
	&patch_version_head_1_9_3,
	&patch_version_1_9_3
};

static const patch_t *model_1_9_3[] = {
	&patch_model0_1_9_3,
	&patch_model1_1_9_3
};

typedef struct {
	const char *date;
	const char *time;
	uint32_t flags;
	size_t n_patches;
	const patch_t **patches;
	size_t n_version_patches;
	const patch_t **version_patches;
	size_t n_model_patches;
	const patch_t **model_patches;
} patchset_t;

#define PATCHSET_FLAG_VERSION	(1 << 0)
#define PATCHSET_FLAG_MODEL	(1 << 1)

static patchset_t patchset_1_9_3 = {
	"Apr 30 2025",
	"10:30:52",
	0,
	N_ARRAY(patches_1_9_3),
	patches_1_9_3,
	N_ARRAY(version_1_9_3),
	version_1_9_3,
	N_ARRAY(model_1_9_3),
	model_1_9_3,
};

static void setup_version_patches(const char *fake_version, int verbose)
{
	uint8_t major_version = 0, minor_version = 0, patch_version = 0;
	char *end;

	const char *p = fake_version;
	major_version = strtoul(p, &end, 10);
	if ((end == p) || (*end != '.')) {
		fprintf(stderr, "%s: can't parse version '%s'\n", progname, fake_version);
		exit(1);
	}
	p = end + 1;
	minor_version = strtoul(p, &end, 10);
	if ((end == p) || (*end != '.')) {
		fprintf(stderr, "%s: can't parse version '%s'\n", progname, fake_version);
		exit(1);
	}
	p = end + 1;
	patch_version = strtoul(p, &end, 10);
	if ((end == p) || (*end != '\0')) {
		fprintf(stderr, "%s: can't parse version '%s'\n", progname, fake_version);
		exit(1);
	}

	rpl_version_any[0] |= (patch_version << 8);
	rpl_version_any[1] = (major_version << 8) | minor_version;

	if (verbose) {
		printf("%s: patch version to %u.%u.%u\n", progname, major_version, minor_version, patch_version);
	}
}

static void setup_model_patches(const char *model, int verbose)
{
	int model_no = 3, shifter = 1, display = 1;
	uint8_t model_byte;
	char *end;

	const char *p = model;
	model_no = strtoul(p, &end, 10);
	if ((end == p) || (*end != ',')) {
		fprintf(stderr, "%s: can't parse model '%s'\n", progname, model);
		exit(1);
	}
	p = end + 1;
	shifter = strtoul(p, &end, 10);
	if ((end == p) || (*end != ',')) {
		fprintf(stderr, "%s: can't parse model '%s'\n", progname, model);
		exit(1);
	}
	p = end + 1;
	display = strtoul(p, &end, 10);
	if ((end == p) || (*end != '\0')) {
		fprintf(stderr, "%s: can't parse model '%s'\n", progname, model);
		exit(1);
	}

	if (model_no != 3 && model_no != 4) {
		fprintf(stderr, "%s: can't parse model '%s': model must be 3 or 4\n", progname, model);
		exit(1);
	}
	if (shifter != 0 && shifter != 1) {
		fprintf(stderr, "%s: can't parse model '%s': shifter must be 0 or 1\n", progname, model);
		exit(1);
	}
	if (display != 0 && display != 1) {
		fprintf(stderr, "%s: can't parse model '%s': display must be 0 or 1\n", progname, model);
		exit(1);
	}

	model_byte = 0;
	if (model_no == 4) {
		model_byte |= 1;
	}
	if (shifter) {
		model_byte |= 2;
	}
	if (display) {
		model_byte |= 4;
	}

	rpl_model_any[0] |= model_byte;

	if (verbose) {
		printf("%s: patch model to %s with%s shifter with%s display\n", progname,
		       model_byte & 1 ? "ES4" : "ES3", model_byte & 2 ? "" : "out", model_byte & 4 ? "" : "out");
	}
}

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

static int verify_patch(const char *filename, const void *data, const patch_t *patch, int verbose)
{
	uint32_t offset = patch->offset - MAINWARE_OFFSET;
	const uint16_t* inst = data + offset;

	for (size_t i = 0; i < patch->size; i++) {
		if (inst[i] != patch->expect[i]) {
			fprintf(stderr, "%s: patch \"%s\": @0x%08x: inst[%zu] 0x%04x != expected 0x%04x\n",
				filename, patch->name, patch->offset, i, inst[i], patch->expect[i]);
			return -1;
		}
	}

	if (verbose) {
		printf("%s: verify \"%s\": @0x%08x [%u]: OK\n", progname, patch->name, patch->offset, patch->size);
	}
	return 0;
}

static int verify_expected(const char *filename, const void *data, const patchset_t *set, int verbose)
{
	int expect_ok = 1;

	for (size_t i = 0; i < set->n_patches; i++) {
		if (verify_patch(filename, data, set->patches[i], verbose) != 0) {
			expect_ok = 0;
		}
	}

	if (set->flags & PATCHSET_FLAG_VERSION) {
		for (size_t i = 0; i < set->n_version_patches; i++) {
			if (verify_patch(filename, data, set->version_patches[i], verbose) != 0) {
				expect_ok = 0;
			}
		}
	}

	if (set->flags & PATCHSET_FLAG_MODEL) {
		for (size_t i = 0; i < set->n_model_patches; i++) {
			if (verify_patch(filename, data, set->model_patches[i], verbose) != 0) {
				expect_ok = 0;
			}
		}
	}

	return expect_ok;
}

static void apply_patch(void *data, const patch_t *patch, int verbose)
{
	uint32_t offset = patch->offset - MAINWARE_OFFSET;
	uint16_t* inst = data + offset;

	for (size_t i = 0; i < patch->size; i++) {
		inst[i] = patch->patch[i];
	}

	if (verbose) {
		printf("%s: apply \"%s\": @0x%08x [%u]\n", progname, patch->name, patch->offset, patch->size);
	}
}

static void apply_patches(void *data, const patchset_t *set, int verbose)
{
	for (size_t i = 0; i < set->n_patches; i++) {
		apply_patch(data, set->patches[i], verbose);
	}

	if (set->flags & PATCHSET_FLAG_VERSION) {
		for (size_t i = 0; i < set->n_version_patches; i++) {
			apply_patch(data, set->version_patches[i], verbose);
		}
	}

	if (set->flags & PATCHSET_FLAG_MODEL) {
		for (size_t i = 0; i < set->n_model_patches; i++) {
			apply_patch(data, set->model_patches[i], verbose);
		}
	}
}

int main(int argc, char** argv)
{
	char *fake_version = NULL;
	char *model = NULL;
	uint32_t flags = 0;
	int verbose = 0;
	int opt;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	while ((opt = getopt(argc, argv, "f:m:v")) != -1) {
		switch (opt) {
			case 'f':
				fake_version = optarg;
				break;
			case 'm':
				model = optarg;
				break;
			case 'v':
				verbose++;
				break;
			default:
				usage();
		}
	}

	if (optind >= argc) {
		usage();
	}

	char *filename = argv[optind];

	if (fake_version) {
		setup_version_patches(fake_version, verbose);
		flags |= PATCHSET_FLAG_VERSION;
	}

	if (model) {
		setup_model_patches(model, verbose);
		flags |= PATCHSET_FLAG_MODEL;
	}

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
			patchset_1_9_3.flags = flags;
			if ((le32toh(ware.crc) == 0x76c1ab9d) && (length == 0x0002fcc8)) {
				if (verify_expected(filename, data, &patchset_1_9_3, verbose)) {
					apply_patches(data, &patchset_1_9_3, verbose);

					memcpy(&ware, data, sizeof(ware));

					memset(ware.date, 0xff, sizeof(ware.date));
					memset(ware.time, 0xff, sizeof(ware.time));

					strcpy(ware.date, patchset_1_9_3.date);
					strcpy(ware.time, patchset_1_9_3.time);

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
