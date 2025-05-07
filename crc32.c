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
#include <zlib.h>

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

	ble_ware_t ble_ware;
	memcpy(&ble_ware, data, sizeof(ble_ware));

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

		uint32_t crc = ware_crc(initial_crc, &ware, data, length);

		printf("%s: CRC 0x%08x %s\n", filename, crc, crc == le32toh(ware.crc) ? "OK" : "FAIL");

		if (crc != le32toh(ware.crc))
			exit(1);
	} else if (memcmp(ble_ware.magic, BLE_WARE_MAGIC, sizeof(ble_ware.magic)) == 0) {
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

		ble_ware_seg_t seg;
		size_t offset = le32toh(ble_ware.hdr_len);

		while (offset < st.st_size) {
			memcpy(&seg, data + offset, sizeof(ble_ware_seg_t));
			printf("%s: BLE ware seg type 0x%02x\n", filename, seg.seg_type);
			printf("%s: BLE ware seg len 0x%08x\n", filename, le32toh(seg.seg_len));

			if (seg.seg_type == BLE_SEG_TYPE_SIGNATURE) {
				ble_ware_signature_seg_t sig;

				memcpy(&sig, data + offset + sizeof(ble_ware_seg_t),
				       le32toh(seg.seg_len) - sizeof(ble_ware_seg_t));
				printf("%s: BLE ware signature ver 0x%02x\n", filename, sig.sig_ver);
				printf("%s: BLE ware signature timestamp 0x%08x\n", filename, le32toh(sig.timestamp));
				printf("%s: BLE ware signature signer %02x %02x %02x %02x %02x %02x %02x %02x\n",
				       filename, sig.ecdsa_signer[0], sig.ecdsa_signer[1], sig.ecdsa_signer[2],
				       sig.ecdsa_signer[3], sig.ecdsa_signer[4], sig.ecdsa_signer[5],
				       sig.ecdsa_signer[6], sig.ecdsa_signer[7]);
				printf("%s: BLE ware signature %02x %02x %02x %02x ...\n",
				       filename, sig.ecdsa_signature[0], sig.ecdsa_signature[1],
				       sig.ecdsa_signature[2], sig.ecdsa_signature[3]);
			}

			offset += le32toh(seg.seg_len);
		}
	} else {
		printf("%s: vanmoof ware magic not found, assume boot-loader binary\n", filename);

		uint32_t expected_crc = le32toh(*(uint32_t *)(data + st.st_size - sizeof(uint32_t)));
		uint8_t *version = (uint8_t *)(data + st.st_size - 2 * sizeof(uint32_t));

		printf("%s: version %c%c%c\n", filename, version[3], version[2], version[1]);
		printf("%s: expected CRC 0x%08x\n", filename, expected_crc);

		uint32_t crc = crc32_calculate(initial_crc, data, st.st_size - sizeof(uint32_t));

		printf("%s: CRC 0x%08x %s\n", filename, crc, crc == expected_crc ? "OK" : "FAIL");

		if (crc != expected_crc)
			exit(1);
	}

	return 0;
}
