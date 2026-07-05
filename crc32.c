#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <endian.h>
#include <zlib.h>

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/asn1.h>

#include "ware.h"
#include "pack.h"

static char *progname;

static void
usage(void)
{
        fprintf(stderr, "usage: %s [-w] <binfile>\n", progname);
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

/*
 * VanMoof bootloaders are pure ARM images with an 8-byte trailer: a
 * 3-character ASCII version (e.g. "007") followed by a CRC-32 over the rest
 * of the image. Returns 1 when the trailing CRC validates (i.e. it really is
 * a VanMoof bootloader, so the version is meaningful).
 */
static int bootloader_trailer(const void *data, size_t size, uint32_t *crc_out, uint32_t *expected_out)
{
	if (size < 2 * sizeof(uint32_t))
		return 0;
	*expected_out = le32toh(*(const uint32_t *)((const uint8_t *)data + size - sizeof(uint32_t)));
	*crc_out = crc32_calculate(initial_crc, data, size - sizeof(uint32_t));
	return *crc_out == *expected_out;
}

/*
 * Some bootloaders (e.g. the CC2642 bleboot) embed a "BVER" build stamp:
 * the tag, then __DATE__ (12 B) and __TIME__ (null-terminated), then 3
 * version bytes major.minor.patch. Returns a pointer to the tag or NULL.
 */
static const uint8_t *find_bytes(const uint8_t *data, size_t size, const char *tag, size_t tl)
{
	if (size < tl)
		return NULL;
	for (size_t i = 0; i + tl <= size; i++)
		if (memcmp(data + i, tag, tl) == 0)
			return data + i;
	return NULL;
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

/*
 * Finalise an application ware in place (the `-w` path): set the length field
 * to the file size, then write ware_crc over the whole image (crc+length
 * blanked) into the crc field. Reuses ware_crc/crc32_calculate above — the same
 * MPEG-2 CRC the STM32 hardware unit and the OEM build compute — so a freshly
 * built image (e.g. backupcode.bin) is accepted by the boot loader. Returns 0.
 */
static int stamp_ware(uint8_t *img, size_t size)
{
	vanmoof_ware_t ware;

	if (size < sizeof(ware) || size % sizeof(uint32_t) != 0) {
		fprintf(stderr, "%s: image size 0x%zx is not a word-aligned ware\n",
			progname, size);
		return 1;
	}

	memcpy(&ware, img, sizeof(ware));
	ware.length = htole32((uint32_t)size);
	ware.crc = htole32(ware_crc(initial_crc, &ware, img, size));
	memcpy(img, &ware, sizeof(ware));

	printf("%s: stamped ware crc 0x%08x length 0x%08zx\n",
	       progname, le32toh(ware.crc), size);
	return 0;
}

static int test_arm(uint8_t *data, size_t len)
{
	if (data[3] != 0x20) // stack pointer must be inside RAM
		return 0;

	uint32_t *p = (uint32_t *)(data + 4);
	uint32_t match = 0;
	int vcount = 0;
	int mcount = 0;

	// Test for similar vectors
	for (int i = 0; i < 15; i++) {
		uint32_t offset = le32toh(p[i]);
		if (offset) {
			vcount++;
			if (match && !((match ^ offset) & 0xffff0000)) {
				mcount++;
			} else {
				match = offset;
				mcount = 1;
			}
		}
	}
	if (vcount > 5 && (vcount - mcount) < 3)
		return 1;

	return 0;
}

/*
 * Identify and CRC-check one image. `prefix` is printed at the start of each
 * line (the file name, or "<file> > <entry>" for a file inside a PACK).
 * Returns 0 when the image is OK/informational (including formats we can't
 * verify), 1 when a recognised image fails its CRC or is truncated. `nested`
 * is set when we are already inside a PACK, so a PACK-in-a-PACK (e.g. the
 * bundled animations.pak) is summarised instead of recursed into. `depth`
 * guards against pathological nesting.
 */
static int analyze(const char *prefix, uint8_t *img, size_t size, int depth, int nested)
{
	if (depth > 8) {
		printf("%s: nesting too deep, stopping\n", prefix);
		return 1;
	}

	vanmoof_ware_t ware;
	ble_ware_t ble_ware;
	int have_ware = size >= sizeof(ware);
	int have_ble = size >= sizeof(ble_ware);

	if (have_ware)
		memcpy(&ware, img, sizeof(ware));
	if (have_ble)
		memcpy(&ble_ware, img, sizeof(ble_ware));

	if (have_ware && le32toh(ware.magic) == WARE_MAGIC) {
		printf("%s: vanmoof ware magic OK\n", prefix);
		printf("%s: vanmoof ware version %x.%x.%x (0x%02x == %s)\n", prefix,
			ware.version[3], ware.version[2], ware.version[1], ware.version[0],
			ware_type_name(ware.version[0]));
		printf("%s: vanmoof ware CRC 0x%08x\n", prefix, le32toh(ware.crc));
		printf("%s: vanmoof ware length 0x%08x\n", prefix, le32toh(ware.length));
		printf("%s: vanmoof ware date %s\n", prefix, ware.date);
		printf("%s: vanmoof ware time %s\n", prefix, ware.time);

		uint32_t length = le32toh(ware.length);
		if (length > size) {
			printf("%s: vanmoof ware length 0x%08x extends beyond image size 0x%08zx\n",
				prefix, length, size);
			return 1;
		}

		uint32_t crc = ware_crc(initial_crc, &ware, img, length);
		printf("%s: CRC 0x%08x %s\n", prefix, crc, crc == le32toh(ware.crc) ? "OK" : "FAIL");
		return crc == le32toh(ware.crc) ? 0 : 1;
	} else if (have_ble && memcmp(ble_ware.magic, BLE_WARE_MAGIC, sizeof(ble_ware.magic)) == 0) {
		printf("%s: BLE ware magic OK\n", prefix);
		printf("%s: BLE ware version %08x\n", prefix, le32toh(ble_ware.soft_ver));
		printf("%s: BLE ware CRC 0x%08x\n", prefix, le32toh(ble_ware.crc));
		printf("%s: BLE ware length 0x%08x\n", prefix, le32toh(ble_ware.len));

		uint32_t length = le32toh(ble_ware.len);
		if (length > size) {
			printf("%s: BLE ware length 0x%08x extends beyond image size 0x%08zx\n",
				prefix, length, size);
			return 1;
		}

		uint32_t crc = crc32(0, img + 12, length - 12);
		printf("%s: CRC 0x%08x %s\n", prefix, crc, crc == le32toh(ble_ware.crc) ? "OK" : "FAIL");

		if (crc != le32toh(ble_ware.crc))
			return 1;

		printf("%s: BLE ware entry 0x%08x\n", prefix, le32toh(ble_ware.prg_entry));
		printf("%s: BLE ware hdr len 0x%08x\n", prefix, le32toh(ble_ware.hdr_len));

		ble_ware_seg_t seg;
		size_t offset = le32toh(ble_ware.hdr_len);

		while (offset + sizeof(seg) <= size) {
			memcpy(&seg, img + offset, sizeof(ble_ware_seg_t));
			printf("%s: BLE ware seg type 0x%02x\n", prefix, seg.seg_type);
			printf("%s: BLE ware seg len 0x%08x\n", prefix, le32toh(seg.seg_len));

			if (seg.seg_type == BLE_SEG_TYPE_SECURITY &&
			    le32toh(seg.seg_len) > sizeof(ble_ware_seg_t) &&
			    offset + le32toh(seg.seg_len) <= size) {
				ble_ware_signature_seg_t sig;

				/* seg_len is read from the image; clamp the copy to
				 * the struct (and we already bounded it to `size`). */
				size_t copy = le32toh(seg.seg_len) - sizeof(ble_ware_seg_t);
				if (copy > sizeof(sig))
					copy = sizeof(sig);
				memset(&sig, 0, sizeof(sig));
				memcpy(&sig, img + offset + sizeof(ble_ware_seg_t), copy);
				printf("%s: BLE ware signature ver 0x%02x\n", prefix, sig.sig_ver);
				printf("%s: BLE ware signature timestamp 0x%08x\n", prefix, le32toh(sig.timestamp));
				printf("%s: BLE ware signature signer %02x %02x %02x %02x %02x %02x %02x %02x\n",
				       prefix, sig.ecdsa_signer[0], sig.ecdsa_signer[1], sig.ecdsa_signer[2],
				       sig.ecdsa_signer[3], sig.ecdsa_signer[4], sig.ecdsa_signer[5],
				       sig.ecdsa_signer[6], sig.ecdsa_signer[7]);
				printf("%s: BLE ware signature %02x %02x %02x %02x ...\n",
				       prefix, sig.ecdsa_signature[0], sig.ecdsa_signature[1],
				       sig.ecdsa_signature[2], sig.ecdsa_signature[3]);
			}

			if (le32toh(seg.seg_len) == 0)
				break;
			offset += le32toh(seg.seg_len);
		}
		return 0;
	} else if (have_ware && le32toh(ware.magic) == HEAD_MAGIC) {
		if (size < sizeof(vanmoof_head_t)) {
			printf("%s: HEAD magic but image too small\n", prefix);
			return 1;
		}
		vanmoof_head_t head;
		memcpy(&head, img, sizeof(head));
		size_t pack_start = le32toh(head.offset);
		size_t pack_len = le32toh(head.length);
		printf("%s: Vanmoof software: Version %d.%d.%d.%d, Offset 0x%x, Length 0x%x\n",
			prefix, (le32toh(head.version0) >> 0) & 0xff, (le32toh(head.version0) >> 8) & 0xff,
			(le32toh(head.version0) >> 16) & 0xff, le32toh(head.version1),
			le32toh(head.offset), le32toh(head.length));

		if (pack_start + pack_len < size) {
			size_t sig_offset = pack_start + pack_len;
			size_t sig_length = size - sig_offset;
			image_tlv_t tlv;
			memcpy(&tlv, img + sig_offset, sizeof(image_tlv_t));

			if ((tlv.type == IMAGE_TLV_INFO_MAGIC) && (tlv.length == sig_length)) {
				printf("%s: Vanmoof signature: Offset 0x%zx, Magic 0x%x, Length 0x%x\n",
				       prefix, sig_offset, tlv.type, tlv.length);

				size_t offset = sizeof(image_tlv_t);
				while (offset < sig_length) {
					BIO *bio;
					uint8_t buffer[32];
					memcpy(&tlv, img + sig_offset + offset, sizeof(image_tlv_t));
					offset += sizeof(image_tlv_t);
					switch (tlv.type) {
						case IMAGE_TLV_SHA256:
							EVP_Digest(img, sig_offset, buffer, NULL, EVP_sha256(), NULL);
							printf("%s: Vanmoof signature: SHA256 at 0x%zx, Length 0x%x: %s\n",
								prefix, sig_offset + offset, tlv.length,
								memcmp(buffer, img + sig_offset + offset, tlv.length) == 0 ? "OK" : "FAIL");
							break;
						case IMAGE_TLV_KEYHASH:
							printf("%s: Vanmoof signature: KEYHASH at 0x%zx, Length 0x%x\n",
								prefix, sig_offset + offset, tlv.length);
							break;
						case IMAGE_TLV_ECDSA_SIG:
							bio = BIO_new_fd(fileno(stdout), BIO_NOCLOSE);
							printf("%s: Vanmoof signature: ECDSA_SIG at 0x%zx, Length 0x%x\n",
								prefix, sig_offset + offset, tlv.length);
							ASN1_parse_dump(bio, img + sig_offset + offset, tlv.length, 0, -1);
							BIO_free(bio);
							break;
						default:
							printf("%s: Vanmoof signature: Type 0x%04x at 0x%zx, Length 0x%x\n",
								prefix, tlv.type, sig_offset + offset, tlv.length);
							break;
					}
					offset += tlv.length;
				}
			} else {
				printf("%s: Unknown trailer: Offset 0x%zx, Length 0x%zx, Magic 0x%x\n",
				       prefix, sig_offset, sig_length, tlv.type);
			}
		}

		if (pack_start + pack_len > size) {
			printf("%s: HEAD payload (0x%zx+0x%zx) extends beyond image 0x%zx\n",
				prefix, pack_start, pack_len, size);
			return 1;
		}

		/* The wrapped payload is itself an image (a single ware, or a
		 * PACK bundle of them) — recurse on it. */
		return analyze(prefix, img + pack_start, pack_len, depth + 1, nested);
	} else if (size >= sizeof(pack_header_t) &&
		   memcmp(img, PACK_MAGIC, sizeof(((pack_header_t *)0)->magic)) == 0) {
		pack_header_t ph;
		memcpy(&ph, img, sizeof(ph));
		size_t dir_off = le32toh(ph.offset);
		size_t dir_len = le32toh(ph.length);
		unsigned count = dir_len / sizeof(pack_entry_t);

		if (dir_off + dir_len > size) {
			printf("%s: PACK directory (0x%zx+0x%zx) extends beyond image 0x%zx\n",
				prefix, dir_off, dir_len, size);
			return 1;
		}

		/* Don't descend into a bundled PACK (e.g. animations.pak full of
		 * UI/sound assets); just report it. */
		if (nested) {
			printf("%s: nested PACK file, %u entries (use unpack to extract)\n",
				prefix, count);
			return 0;
		}

		printf("%s: PACK file, %u entries\n", prefix, count);

		int rc = 0;
		for (unsigned i = 0; i < count; i++) {
			pack_entry_t e;
			memcpy(&e, img + dir_off + i * sizeof(e), sizeof(e));

			char name[sizeof(e.filename) + 1];
			memcpy(name, e.filename, sizeof(e.filename));
			name[sizeof(e.filename)] = '\0';

			size_t eoff = le32toh(e.offset);
			size_t elen = le32toh(e.length);
			printf("%s: PACK entry %u: %s offset 0x%zx length 0x%zx\n",
				prefix, i, name, eoff, elen);

			if (eoff + elen > size || eoff + elen < eoff) {
				printf("%s > %s: entry extends beyond image, skipping\n", prefix, name);
				rc = 1;
				continue;
			}

			char sub[512];
			snprintf(sub, sizeof(sub), "%s > %s", prefix, name);
			rc |= analyze(sub, img + eoff, elen, depth + 1, 1);
		}
		return rc;
	} else if (size > VMFW_OFFSET + sizeof(vmfw_ware_t) &&
		   memcmp(img + VMFW_OFFSET, VMFW_MAGIC, sizeof(((vmfw_ware_t *)0)->magic)) == 0) {
		/* VanMoof S5/A5 Cortex-M ECU image: "VMFW" header at 0x134. */
		vmfw_ware_t vmfw;
		memcpy(&vmfw, img + VMFW_OFFSET, sizeof(vmfw));

		uint8_t *h = img + VMFW_OFFSET;
		uint32_t version = le32toh(vmfw.version);

		printf("%s: VMFW magic OK at offset 0x%x\n", prefix, VMFW_OFFSET);

		/*
		 * Two header dialects share this magic/crc/length but differ in
		 * the bytes after `length`:
		 *   S5/A5: __DATE__ (12) + __TIME__ (12); version is manifest-
		 *          packed (major<<24 | minor<<16 | variant<<13 | patch),
		 *          e.g. 0x01056000 = 1.5.0 main.
		 *   S6:    a uint32 build number, then a "vMAJOR.MINOR.PATCH.BUILD"
		 *          string; version is plain bytes major.minor.patch,
		 *          e.g. 0x00080801 = 1.8.8 build 4974 ("v1.8.8.4974").
		 * The S6 dialect is recognised by that 'v'+digit version string.
		 */
		if (h[20] == 'v' && isdigit((unsigned char)h[21])) {
			uint32_t build;
			memcpy(&build, h + 16, sizeof(build));
			build = le32toh(build);
			printf("%s: VMFW version %u.%u.%u build %u (%.20s, 0x%08x)\n", prefix,
				version & 0xff, (version >> 8) & 0xff, (version >> 16) & 0xff,
				build, (const char *)(h + 20), version);
			printf("%s: VMFW CRC 0x%08x\n", prefix, le32toh(vmfw.crc));
			printf("%s: VMFW length 0x%08x\n", prefix, le32toh(vmfw.length));
		} else {
			uint32_t variant = vmfw_version_variant(version);
			printf("%s: VMFW version %u.%u.%u %s (0x%08x)\n", prefix,
				vmfw_version_major(version), vmfw_version_minor(version),
				vmfw_version_patch(version), vmfw_variant_name(variant), version);
			printf("%s: VMFW CRC 0x%08x\n", prefix, le32toh(vmfw.crc));
			printf("%s: VMFW length 0x%08x\n", prefix, le32toh(vmfw.length));
			printf("%s: VMFW date %.12s\n", prefix, vmfw.date);
			printf("%s: VMFW time %.12s\n", prefix, vmfw.time);
		}

		/*
		 * The header length is authoritative: it is the size of the
		 * image to CRC (and flash). A length that runs past the image is
		 * a truncated/corrupt image, so bail like the ware/BLE branches
		 * do; a length shorter than the image just means trailing data
		 * (e.g. a signature) that is not part of the CRC'd image.
		 */
		uint32_t length = le32toh(vmfw.length);
		if (length > size) {
			printf("%s: VMFW length 0x%08x extends beyond image size 0x%08zx\n",
				prefix, length, size);
			return 1;
		}
		if (length < VMFW_OFFSET + offsetof(vmfw_ware_t, crc) + 8) {
			printf("%s: VMFW length 0x%08x too small to be a valid image\n",
				prefix, length);
			return 1;
		}
		if (length != size)
			printf("%s: VMFW length 0x%08x is shorter than image size 0x%08zx (0x%08zx trailing bytes)\n",
				prefix, length, size, size - length);

		/*
		 * Standard CRC-32 over the image (the header's `length` bytes)
		 * with the crc and length header fields (8 bytes at
		 * VMFW_OFFSET+offsetof(crc)) replaced by 0xffffffff. zlib's
		 * crc32() is the matching algorithm.
		 */
		static const uint8_t ff8[8] = {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
		};
		size_t fields_off = VMFW_OFFSET + offsetof(vmfw_ware_t, crc);
		uint32_t crc = crc32(0, img, fields_off);
		crc = crc32(crc, ff8, sizeof(ff8));
		crc = crc32(crc, img + fields_off + sizeof(ff8),
			    length - fields_off - sizeof(ff8));

		printf("%s: CRC 0x%08x %s\n", prefix, crc,
			crc == le32toh(vmfw.crc) ? "OK" : "FAIL");
		return crc == le32toh(vmfw.crc) ? 0 : 1;
	} else if (size >= 64 && test_arm(img, size)) {
		printf("%s: Pure ARM binary, Length 0x%zx\n", prefix, size);

		/* A bootloader is also a pure ARM image; report its version+CRC
		 * trailer when present (older bootloaders, e.g. BL V004, leave it
		 * blank and the CRC won't validate — then it's just a plain ARM
		 * binary and we say nothing further). */
		/* bmsboot prints its version + build date in a banner string
		 * ("… VanMoof BL V<ver> <date>"). The version+CRC trailer at the
		 * image tail only carries the 3-digit version (no date), and older
		 * builds (e.g. V004) leave it blank — so read the banner for the
		 * version/date and use the trailer only for the self-CRC check. */
		{
			const char *needle = "VanMoof BL V";
			size_t nl = strlen(needle);
			const uint8_t *end = img + size;
			for (const uint8_t *p = img; p + nl + 4 <= end; p++) {
				if (memcmp(p, needle, nl) != 0)
					continue;
				const char *ver = (const char *)(p + nl);
				const char *date = ver + 3;
				if (date < (const char *)end && *date == ' ')
					date++;
				if (date >= (const char *)end || *date <= ' ')
					continue;	/* no date after this banner (e.g. "V006 ") */
				size_t dl = 0;
				while (date + dl < (const char *)end &&
				       date[dl] != '\r' && date[dl] != '\n' && date[dl] != '\0')
					dl++;
				printf("%s: bootloader version %.3s (%.*s)\n", prefix, ver, (int)dl, date);
				break;
			}
		}

		uint32_t crc, expected_crc;
		if (bootloader_trailer(img, size, &crc, &expected_crc))
			printf("%s: bootloader CRC 0x%08x OK\n", prefix, crc);

		/* mainboot (muco-boot) carries a vanmoof_ware_t in the LAST 0x28
		 * bytes instead of at the start: magic, version (major.minor in
		 * version[3]/[2]), then date/time. Its crc/length are left unset
		 * (0xffffffff) — the loader is not self-CRC'd; it CRC-checks the
		 * application images instead. */
		if (size >= sizeof(vanmoof_ware_t)) {
			vanmoof_ware_t foot;
			memcpy(&foot, img + size - sizeof(foot), sizeof(foot));
			if (le32toh(foot.magic) == WARE_MAGIC) {
				printf("%s: bootloader version %x.%02x (%.12s %.12s)\n", prefix,
					foot.version[3], foot.version[2], foot.date, foot.time);
				if (le32toh(foot.crc) != 0xffffffff)
					printf("%s: bootloader CRC 0x%08x\n", prefix, le32toh(foot.crc));
				else
					printf("%s: bootloader CRC not set (loader is not self-CRC'd)\n", prefix);
			}
		}

		/* CC2642 bleboot-style "BVER" build stamp (tag, __DATE__ (12 B),
		 * null-terminated __TIME__, then 3 version bytes major.minor.patch).
		 * The image integrity CRC for these lives in the TI OAD "OAD NVM1"
		 * image header, not in a VanMoof trailer. */
		const uint8_t *bver = find_bytes(img, size, "BVER", 4);
		if (bver) {
			const uint8_t *end = img + size;
			const char *date = (const char *)(bver + 4);
			const char *time = date + strnlen(date, end - (const uint8_t *)date) + 1;
			const uint8_t *ver = (const uint8_t *)time + strnlen(time, end - (const uint8_t *)time) + 1;
			if (ver + 3 <= end)
				printf("%s: BVER version %u.%u.%u (%.12s %s)\n", prefix,
					ver[0], ver[1], ver[2], date, time);
		}
		return 0;
	} else {
		/* Unknown to us. It may still be a headerless bootloader whose
		 * tail carries an ASCII version + self-CRC; only claim that when
		 * the trailer actually validates (or at least looks like a
		 * version), otherwise just say we can't verify it instead of
		 * printing a bogus version + CRC FAIL. */
		uint32_t crc, expected_crc;
		int ok = size >= 2 * sizeof(uint32_t) &&
			 bootloader_trailer(img, size, &crc, &expected_crc);
		const uint8_t *v = img + size - 2 * sizeof(uint32_t);
		int looks_like_version = size >= 2 * sizeof(uint32_t) &&
			isprint(v[1]) && isprint(v[2]) && isprint(v[3]);

		if (ok) {
			printf("%s: assume boot-loader binary\n", prefix);
			printf("%s: bootloader version %c%c%c\n", prefix, v[3], v[2], v[1]);
			printf("%s: bootloader CRC 0x%08x OK\n", prefix, crc);
			return 0;
		} else if (looks_like_version) {
			printf("%s: assume boot-loader binary\n", prefix);
			printf("%s: bootloader version %c%c%c\n", prefix, v[3], v[2], v[1]);
			printf("%s: expected CRC 0x%08x\n", prefix, expected_crc);
			printf("%s: CRC 0x%08x FAIL\n", prefix, crc);
			return 1;
		}

		printf("%s: unrecognized image format (first bytes 0x%08x), cannot verify\n",
			prefix, have_ware ? le32toh(ware.magic) : 0);
		return 0;
	}
}

int main(int argc, char** argv)
{
	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	int do_write = 0;
	int argi = 1;

	if (argc > argi && strcmp(argv[argi], "-w") == 0) {
		do_write = 1;                  /* stamp crc/length in place (ware images) */
		argi++;
	}

	if (argc <= argi)
		usage();

	char *filename = argv[argi];

	int fd = open(filename, do_write ? O_RDWR : O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: open(%s): %s\n", progname, filename, strerror(errno));
		exit(1);
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "%s: stat(%s): %s\n", progname, filename, strerror(errno));
		exit(1);
	}

	void *data = mmap(NULL, st.st_size,
			  do_write ? (PROT_READ | PROT_WRITE) : PROT_READ,
			  MAP_SHARED, fd, 0);
	if (data == (void *)-1) {
		fprintf(stderr, "%s: mmap(%s): %s\n", progname, filename, strerror(errno));
		exit(1);
	}

	close(fd);

	if (do_write) {
		if ((size_t)st.st_size >= sizeof(vanmoof_ware_t) &&
		    le32toh(*(uint32_t *)data) == WARE_MAGIC) {
			if (stamp_ware((uint8_t *)data, st.st_size) == 0)
				msync(data, st.st_size, MS_SYNC);
		} else {
			fprintf(stderr, "%s: %s: no 0x%08x ware magic, not stamping\n",
				progname, filename, WARE_MAGIC);
		}
	}

	int rc = analyze(filename, (uint8_t *)data, st.st_size, 0, 0);

	munmap(data, st.st_size);

	return rc ? 1 : 0;
}
