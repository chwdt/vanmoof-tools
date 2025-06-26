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
#include <zlib.h>

#include "ware.h"

static char *progname;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-v] <binfile>\n", progname);
	exit(1);
}

static const uint16_t exp_full_debug[] = {
	0xf440,
	0x7040,
};

static const uint16_t rpl_full_debug[] = {
	0xf240,
	0x30ff,
};

static const uint16_t exp_null_ptr[] = {
	0x0000, 0x0000,
};

static const uint16_t rpl_exc_dump[] = {
	0xbeb5, 0x1002, // ti_sysbios_family_arm_m3_Hwi_excHandlerMax
};

static const uint16_t exp_error_spin[] = {
	0xf1f7, 0x1002,
};

static const uint16_t rpl_error_spin[] = {
	0xfb35, 0x1002, // ti_sysbios_family_arm_m3_Hwi_return
};

static const uint16_t exp_system_putchar_1_4_1[] = {
	0x60f9, 0x0002,
};

static const uint16_t rpl_system_putchar_1_4_1[] = {
	(SYSTEM_PUTCHAR1 & 0xffff) | 1, SYSTEM_PUTCHAR1 >> 16,
};

static const uint16_t exp_system_putchar_2_4_1[] = {
	0xdbc9, 0x0002,
};

static const uint16_t rpl_system_putchar_2_4_1[] = {
	(SYSTEM_PUTCHAR2 & 0xffff) | 1, SYSTEM_PUTCHAR2 >> 16,
};

static const uint16_t exp_offset_rtos_stat_1_4_1[] = {
	0xf6a1, 0x0000,
};

static const uint16_t exp_offset_rtos_stat_2_4_1[] = {
	0x0fa5, 0x0001,
};

static const uint16_t rpl_offset_rtos_stat_1_4_1[] = {
	0xc67d, 0x0002,
};

static const uint16_t rpl_offset_rtos_stat_2_4_1[] = {
	0x531d, 0x0003,
};

static const uint16_t rpl_dump_1_4_1[] = {
#include "keys1.hex"
};

static const uint16_t rpl_dump_2_4_1[] = {
#include "keys2.hex"
};

static const char exp_date_time_1_4_1[] = {
	"Mar 29 2021 / 14:20:30",
};

static const char exp_date_time_2_4_1[] = {
	"Mar 29 2021 / 14:17:30",
};

static const char rpl_date_time_1_4_1[] = {
	"May 12 2025 / 09:03:35",
};

static const char rpl_date_time_2_4_1[] = {
	"Jun 10 2025 / 12:02:27",
};

typedef struct {
	const char* name;
	off_t offset;
	size_t size;
	const uint16_t *expect;
	const uint16_t *patch;
} patch_t;

#define N_ARRAY(a) (sizeof(a) / sizeof(a[0]))

static const patch_t patch_full_debug_1_4_1 = {
	"enable debug",
	0x1cfda,
	N_ARRAY(rpl_full_debug),
	exp_full_debug,
	rpl_full_debug,
};

static const patch_t patch_full_debug_2_4_1 = {
	"enable debug",
	0x22306,
	N_ARRAY(rpl_full_debug),
	exp_full_debug,
	rpl_full_debug,
};

static const patch_t patch_exc_dump_1_4_1 = {
	"exception dump",
	0x294d8,
	N_ARRAY(rpl_exc_dump),
	exp_null_ptr,
	rpl_exc_dump,
};

static const patch_t patch_exc_dump_2_4_1 = {
	"exception dump",
	0x31770,
	N_ARRAY(rpl_exc_dump),
	exp_null_ptr,
	rpl_exc_dump,
};

static const patch_t patch_error_spin_1_4_1 = {
	"error spin",
	0x29440,
	N_ARRAY(rpl_error_spin),
	exp_error_spin,
	rpl_error_spin,
};

static const patch_t patch_error_spin_2_4_1 = {
	"error spin",
	0x316d8,
	N_ARRAY(rpl_error_spin),
	exp_error_spin,
	rpl_error_spin,
};

static const patch_t patch_system_putchar_1_4_1 = {
	"system putchar",
	0x29604,
	N_ARRAY(rpl_system_putchar_1_4_1),
	exp_system_putchar_1_4_1,
	rpl_system_putchar_1_4_1,
};

static const patch_t patch_system_putchar_2_4_1 = {
	"system putchar",
	0x3189c,
	N_ARRAY(rpl_system_putchar_2_4_1),
	exp_system_putchar_2_4_1,
	rpl_system_putchar_2_4_1,
};

static const patch_t patch_offset_rtos_stat_1_4_1 = {
	"offset dump",
	0x2a108,
	N_ARRAY(rpl_offset_rtos_stat_1_4_1),
	exp_offset_rtos_stat_1_4_1,
	rpl_offset_rtos_stat_1_4_1,
};

static const patch_t patch_offset_rtos_stat_2_4_1 = {
	"offset dump",
	0x3237c,
	N_ARRAY(rpl_offset_rtos_stat_2_4_1),
	exp_offset_rtos_stat_2_4_1,
	rpl_offset_rtos_stat_2_4_1,
};

static const patch_t patch_dump_1_4_1 = {
	"dump",
	0x2c67c,
	N_ARRAY(rpl_dump_1_4_1),
	NULL,
	rpl_dump_1_4_1,
};

static const patch_t patch_dump_2_4_1 = {
	"dump",
	0x3531c,
	N_ARRAY(rpl_dump_2_4_1),
	NULL,
	rpl_dump_2_4_1,
};

static const patch_t patch_date_time_1_4_1 = {
	"date/time",
	0x570f,
	sizeof(rpl_date_time_1_4_1) / sizeof(uint16_t),
	(uint16_t *)exp_date_time_1_4_1,
	(uint16_t *)rpl_date_time_1_4_1,
};

static const patch_t patch_date_time_2_4_1 = {
	"date/time",
	0x2677,
	sizeof(rpl_date_time_2_4_1) / sizeof(uint16_t),
	(uint16_t *)exp_date_time_2_4_1,
	(uint16_t *)rpl_date_time_2_4_1,
};

static const patch_t *patches_1_4_1[] = {
	&patch_offset_rtos_stat_1_4_1,
	&patch_full_debug_1_4_1,
	&patch_exc_dump_1_4_1,
	&patch_error_spin_1_4_1,
	&patch_system_putchar_1_4_1,
	&patch_dump_1_4_1,
	&patch_date_time_1_4_1,
};

static const patch_t *patches_2_4_1[] = {
	&patch_offset_rtos_stat_2_4_1,
	&patch_full_debug_2_4_1,
	&patch_exc_dump_2_4_1,
	&patch_error_spin_2_4_1,
	&patch_system_putchar_2_4_1,
	&patch_dump_2_4_1,
	&patch_date_time_2_4_1,
};

typedef struct {
	const char *date;
	const char *time;
	size_t n_patches;
	const patch_t **patches;
	size_t n_version_patches;
	const patch_t **version_patches;
} patchset_t;

static const patchset_t patchset_1_4_1 = {
	"May 12 2025",
	"09:03:35",
	N_ARRAY(patches_1_4_1),
	patches_1_4_1,
	0,
	NULL,
};

static const patchset_t patchset_2_4_1 = {
	"Jun 10 2025",
	"12:02:27",
	N_ARRAY(patches_2_4_1),
	patches_2_4_1,
	0,
	NULL,
};

static const uint32_t crc_poly = 0x4c11db7;
static const uint32_t initial_crc = 0;

static int verify_patch(const char *filename, const void *data, const patch_t *patch, int verbose)
{
	uint32_t offset = patch->offset - BLE_WARE_OFFSET;
	const uint16_t* inst = data + offset;

	if (patch->expect) {
		for (size_t i = 0; i < patch->size; i++) {
			if (inst[i] != patch->expect[i]) {
				fprintf(stderr, "%s: patch \"%s\": @0x%08x: inst[%zu] 0x%04x != expected 0x%04x\n",
					filename, patch->name, patch->offset, i, inst[i], patch->expect[i]);
				return -1;
			}
		}
	}

	if (verbose) {
		printf("%s: verify \"%s\": @0x%08x [%u]: OK\n", progname, patch->name, patch->offset, patch->size);
	}
	return 0;
}

static int verify_expected(const char *filename, const void *data, const char* fake_version, const patchset_t *set, int verbose)
{
	int expect_ok = 1;

	for (size_t i = 0; i < set->n_patches; i++) {
		if (verify_patch(filename, data, set->patches[i], verbose) != 0) {
			expect_ok = 0;
		}
	}

	if (fake_version) {
		for (size_t i = 0; i < set->n_version_patches; i++) {
			if (verify_patch(filename, data, set->version_patches[i], verbose) != 0) {
				expect_ok = 0;
			}
		}
	}

	return expect_ok;
}

static void apply_patch(void *data, const patch_t *patch, int verbose)
{
	uint32_t offset = patch->offset - BLE_WARE_OFFSET;
	uint16_t* inst = data + offset;

	for (size_t i = 0; i < patch->size; i++) {
		inst[i] = patch->patch[i];
	}

	if (verbose) {
		printf("%s: apply \"%s\": @0x%08x [%u]\n", progname, patch->name, patch->offset, patch->size);
	}
}

static void apply_patches(void *data, const char *fake_version, const patchset_t *set, int verbose)
{
	for (size_t i = 0; i < set->n_patches; i++) {
		apply_patch(data, set->patches[i], verbose);
	}

	if (fake_version) {
		for (size_t i = 0; i < set->n_version_patches; i++) {
			apply_patch(data, set->version_patches[i], verbose);
		}
	}
}

static void fixup_headers(ble_ware_t *ble_ware, void *data, size_t size, size_t add_len, const char *filename)
{
	ble_ware_seg_t seg;
	size_t offset = le32toh(ble_ware->hdr_len);

	while (offset < size + add_len) {
		memcpy(&seg, data + offset, sizeof(ble_ware_seg_t));
		printf("%s: BLE ware seg type 0x%02x\n", filename, seg.seg_type);
		printf("%s: BLE ware seg len 0x%08x\n", filename, le32toh(seg.seg_len));

		if (seg.seg_type == BLE_SEG_TYPE_CONTIGUOUS) {
			seg.seg_len = htole32(le32toh(seg.seg_len) + add_len);
			memcpy(data + offset, &seg, sizeof(ble_ware_seg_t));
		}
		if (seg.seg_type == BLE_SEG_TYPE_SECURITY) {
			seg.seg_type = BLE_SEG_TYPE_NONCONTIGUOUS;
			memcpy(data + offset, &seg, sizeof(ble_ware_seg_t));
		}

		offset += le32toh(seg.seg_len);
	}

	uint32_t length = le32toh(ble_ware->len) + add_len;
	ble_ware->len = htole32(length);
	ble_ware->img_end_addr = htole32(le32toh(ble_ware->img_end_addr) + add_len);
	memcpy(data, ble_ware, sizeof(ble_ware_t));

	ble_ware->crc = crc32(0, data + 12, length - 12);
	memcpy(data, ble_ware, sizeof(ble_ware_t));

	printf("%s: CRC 0x%08x\n", filename, le32toh(ble_ware->crc));
}

int main(int argc, char** argv)
{
	int verbose = 0;
	int opt;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	while ((opt = getopt(argc, argv, "v")) != -1) {
		switch (opt) {
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

	size_t add_len = sizeof(rpl_dump_1_4_1);
	if (add_len != sizeof(rpl_dump_2_4_1)) {
		fprintf(stderr, "%s: FIXME: sizeof(rpl_dump_1_4_1) and sizeof(rpl_dump_2_4_1) differ\n", progname);
		exit(1);
	}

	void *data = mmap(NULL, st.st_size + add_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == (void *)-1) {
		fprintf(stderr, "%s: mmap(%s): %s\n", progname, filename, strerror(errno));
		exit(1);
	}

	ble_ware_t ble_ware;
	memcpy(&ble_ware, data, sizeof(ble_ware));

	if (memcmp(ble_ware.magic, BLE_WARE_MAGIC, sizeof(ble_ware.magic)) == 0) {
		printf("%s: BLE ware magic OK\n", filename);
		printf("%s: BLE ware version %08x\n", filename, le32toh(ble_ware.soft_ver));
		printf("%s: BLE ware CRC 0x%08x\n", filename, le32toh(ble_ware.crc));
		printf("%s: BLE ware length 0x%08x\n", filename, le32toh(ble_ware.len));

		uint32_t length = le32toh(ble_ware.len);
		if (length > st.st_size) {
			printf("%s: BLE ware length 0x%08x extends beyond file size 0x%08zx\n",
				filename, length, st.st_size);
			exit(1);
		}

		uint32_t crc = crc32(0, data + 12, length - 12);

		printf("%s: CRC 0x%08x %s\n", filename, crc, crc == le32toh(ble_ware.crc) ? "OK" : "FAIL");

		if (crc != le32toh(ble_ware.crc))
			exit(1);

		printf("%s: BLE ware entry 0x%08x\n", filename, le32toh(ble_ware.prg_entry));
		printf("%s: BLE ware hdr len 0x%08x\n", filename, le32toh(ble_ware.hdr_len));

 		if ((le32toh(ble_ware.crc) == 0xb79c4373) && (length == 0x0002c67c)) {
                	if (verify_expected(filename, data, NULL, &patchset_1_4_1, verbose)) {
				if (add_len) {
					ftruncate(fd, st.st_size + add_len);
				}
                        	apply_patches(data, NULL, &patchset_1_4_1, verbose);

				fixup_headers(&ble_ware, data, st.st_size, add_len, filename);
			} else {
				fprintf(stderr, "%s: verify patchset failed\n", progname);
				exit(1);
			}
 		} else if ((le32toh(ble_ware.crc) == 0x884a9283) && (length == 0x0003531c)) {
                	if (verify_expected(filename, data, NULL, &patchset_2_4_1, verbose)) {
				if (add_len) {
					ftruncate(fd, st.st_size + add_len);
				}
                        	apply_patches(data, NULL, &patchset_2_4_1, verbose);

				fixup_headers(&ble_ware, data, st.st_size, add_len, filename);
			} else {
				fprintf(stderr, "%s: verify patchset failed\n", progname);
				exit(1);
			}
		} else {
			fprintf(stderr, "%s: No patchset for this version of bleware.bin\n", progname);
			exit(1);
		}
	} else {
		fprintf(stderr, "%s: Not a vanmoof ware file\n", filename);
		exit(1);
	}

	munmap(data, st.st_size + add_len);

	close(fd);

	return 0;
}
