/*
 * ble-merge - build one flashable CC2642R1F image out of a bleware +
 * bleboot pair, with the CCFG debug lock removed.
 *
 * The VanMoof S3/X3 BLE module (TI CC2642R1F, 352 KB internal flash)
 * keeps its two images in two separate places:
 *
 *   0x00000000  bleware - TI "OAD NVM1" application image: OAD header
 *                         at offset 0, vector table at prgEntry (0x90)
 *   0x00056000  bleboot - the BIM (TI Boot Image Manager), the last
 *                         8 KB page; the 88-byte CCFG sits at its end
 *                         at 0x00057FA8
 *
 * A programmer talking to a mass-erased chip (SmartRF Flash Programmer
 * 2, UniFlash, OpenOCD) wants a single image, so this tool lays both
 * out in one 352 KB flat image - 0xFF everywhere else - and writes it
 * as raw binary or Intel HEX.
 *
 * While doing that it clears the OEM debug lock in the CCFG. VanMoof
 * ships CCFG_TAP_DAP_0 = CCFG_TAP_DAP_1 = 0xFF000000 and
 * CCFG_TI_OPTIONS = 0xFFFFFF00: every TAP-enable field is 0x00 and
 * only the value 0xC5 enables a TAP, so the CPU DAP is off and an
 * XDS110 gets no further than the ICEPick router ("A router subpath
 * could not be accessed. A security error has probably occurred.").
 * Flashing that CCFG back would lock the part again the moment it
 * boots, so ble-merge writes the unlocked values instead - the very
 * same ones ble-patch's `dump ccfg` command writes at runtime
 * (0xFFFFFFC5 / 0xFFC5C5C5 / 0xFFC5C5C5), so a patched bleware
 * recognises its own marker and leaves the boot loader page alone.
 *
 * Nothing else is touched: both images are copied byte for byte.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include <zlib.h>

/* CC2642R1F flash geometry. */
#define FLASH_SIZE		0x58000u	/* 352 KB main flash	*/
#define PAGE_SIZE		0x2000u		/* 8 KB erase page	*/
#define BLEWARE_BASE		0x00000u
#define BLEBOOT_BASE		0x56000u	/* last page		*/

/* BVER block, inside the boot loader page. */
#define BVER_BASE		0x57f38u	/* "BVER" + build date	*/
#define BVER_TIME		0x57f48u	/* build time		*/

/* CCFG (Customer Configuration), 88 bytes at the very end of flash.
 * Absolute flash addresses, i.e. indexes into the merged image. */
#define CCFG_BASE		0x57fa8u
#define CCFG_LEN		0x58u
#define CCFG_BL_CONFIG		0x57fd8u
#define CCFG_ERASE_CONF		0x57fdcu
#define CCFG_TI_OPTIONS		0x57fe0u
#define CCFG_TAP_DAP_0		0x57fe4u
#define CCFG_TAP_DAP_1		0x57fe8u
#define CCFG_IMAGE_VALID_CONF	0x57fecu

/* A TAP-enable field is on only for this exact value. */
#define TAP_ENABLE		0xc5u

/* Unlocked CCFG words - identical to what keys.c writes at runtime. */
#define TI_OPTIONS_OPEN		0xffffffc5u
#define TAP_DAP_OPEN		0xffc5c5c5u

/* TI OAD image header ("OAD NVM1"), offsets from the start of bleware. */
#define OAD_MAGIC		"OAD NVM1"
#define OAD_MAGIC_LEN		8
#define OAD_HDR_LEN		0x2cu		/* fixed part		*/
#define OAD_CRC_SKIP		12u		/* CRC starts here	*/
#define OAD_OFF_CRC32		8
#define OAD_OFF_BIMVER		12
#define OAD_OFF_METAVER		13
#define OAD_OFF_TECHTYPE	14
#define OAD_OFF_IMGCPSTAT	16
#define OAD_OFF_CRCSTAT		17
#define OAD_OFF_IMGTYPE		18
#define OAD_OFF_IMGNO		19
#define OAD_OFF_LEN		24
#define OAD_OFF_PRGENTRY	28
#define OAD_OFF_SOFTVER		32
#define OAD_OFF_ENDADDR		36
#define OAD_OFF_SEGMENTS	44		/* segment list starts	*/

#define OAD_SEG_IMAGE		1		/* contiguous image seg	*/

static char *progname;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-k] [-v] [-x] <blewarefile> <blebootfile> [<outfile>]\n",
		progname);
	exit(1);
}

static uint32_t
rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	       (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void
wr32(uint8_t *p, uint32_t v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

static uint8_t *
read_file(const char *path, size_t *lenp)
{
	struct stat st;
	uint8_t *buf;
	size_t total;
	ssize_t n;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: open(%s): %s\n", progname, path, strerror(errno));
		exit(1);
	}
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "%s: fstat(%s): %s\n", progname, path, strerror(errno));
		exit(1);
	}
	if (st.st_size == 0) {
		fprintf(stderr, "%s: %s: empty file\n", progname, path);
		exit(1);
	}

	buf = malloc(st.st_size);
	if (buf == NULL) {
		fprintf(stderr, "%s: malloc(%zu): Out of memory\n", progname,
			(size_t)st.st_size);
		exit(1);
	}

	total = 0;
	while (total < (size_t)st.st_size) {
		n = read(fd, buf + total, st.st_size - total);
		if (n <= 0) {
			fprintf(stderr, "%s: read(%s): %s\n", progname, path,
				n < 0 ? strerror(errno) : "short read");
			exit(1);
		}
		total += n;
	}
	close(fd);

	*lenp = total;
	return buf;
}

static int
is_oad_image(const uint8_t *buf, size_t len)
{
	return len >= OAD_HDR_LEN && memcmp(buf, OAD_MAGIC, OAD_MAGIC_LEN) == 0;
}

/* The boot loader is the last flash page. Accept either the bare page
 * or a full 352 KB flash dump, in which case the page is sliced out. */
static const uint8_t *
bleboot_page(const uint8_t *buf, size_t len)
{
	if (len == PAGE_SIZE)
		return buf;
	if (len == FLASH_SIZE)
		return buf + BLEBOOT_BASE;
	return NULL;
}

static const char *
tap_state(uint32_t reg, int shift)
{
	return ((reg >> shift) & 0xff) == TAP_ENABLE ? "enabled" : "disabled";
}

/* Walk the OAD segment list the way the BIM's bim_oad_find_image_addr()
 * does: 12-byte descriptors from offset 44, advance by the length word,
 * stop on the contiguous-image segment and report its load address. */
static int
oad_image_address(const uint8_t *buf, uint32_t img_len, uint32_t *addr)
{
	uint32_t offset = OAD_OFF_SEGMENTS;

	while (offset + 12 <= img_len) {
		uint32_t seg_len = rd32(buf + offset + 4);

		if (buf[offset] == OAD_SEG_IMAGE) {
			*addr = rd32(buf + offset + 8);
			return 1;
		}
		if (seg_len == 0)
			break;
		offset += seg_len;
	}
	return 0;
}

static const char *ccfg_field_name[CCFG_LEN / 4] = {
	"EXT_LF_CLK",		"MODE_CONF_1",		"SIZE_AND_DIS_FLAGS",
	"MODE_CONF",		"VOLT_LOAD_0",		"VOLT_LOAD_1",
	"RTC_OFFSET",		"FREQ_OFFSET",		"IEEE_MAC_0",
	"IEEE_MAC_1",		"IEEE_BLE_0",		"IEEE_BLE_1",
	"BL_CONFIG",		"ERASE_CONF",		"CCFG_TI_OPTIONS",
	"CCFG_TAP_DAP_0",	"CCFG_TAP_DAP_1",	"IMAGE_VALID_CONF",
	"CCFG_PROT_31_0",	"CCFG_PROT_63_32",	"CCFG_PROT_95_64",
	"CCFG_PROT_127_96",
};

static void
dump_ccfg(const uint8_t *page)
{
	unsigned i;

	for (i = 0; i < CCFG_LEN / 4; i++) {
		uint32_t off = CCFG_BASE - BLEBOOT_BASE + i * 4;

		printf("          0x%05x  %-18s 0x%08x\n",
			BLEBOOT_BASE + off, ccfg_field_name[i], rd32(page + off));
	}
}

/* Intel HEX output. Records are 16 bytes; a type 04 extended linear
 * address record is emitted whenever the upper 16 address bits change. */
static int hex_ela = -1;

static void
hex_record(FILE *f, uint8_t type, uint16_t addr, const uint8_t *data, uint8_t len)
{
	uint8_t sum = len + (addr >> 8) + (addr & 0xff) + type;
	unsigned i;

	fprintf(f, ":%02X%04X%02X", len, addr, type);
	for (i = 0; i < len; i++) {
		fprintf(f, "%02X", data[i]);
		sum += data[i];
	}
	fprintf(f, "%02X\n", (uint8_t)-sum);
}

static void
hex_region(FILE *f, const uint8_t *image, uint32_t start, uint32_t end)
{
	uint32_t addr;

	for (addr = start; addr < end; ) {
		uint32_t n = end - addr;

		if (n > 16)
			n = 16;
		if ((int)(addr >> 16) != hex_ela) {
			uint8_t ela[2];

			hex_ela = addr >> 16;
			ela[0] = hex_ela >> 8;
			ela[1] = hex_ela & 0xff;
			hex_record(f, 0x04, 0, ela, 2);
		}
		hex_record(f, 0x00, addr & 0xffff, image + addr, n);
		addr += n;
	}
}

int
main(int argc, char **argv)
{
	const char *ware_path, *boot_path, *out_path;
	const uint8_t *boot_page;
	uint8_t *ware, *boot, *image;
	size_t ware_len, boot_len;
	uint32_t img_len, img_crc, calc_crc, entry, end_addr, load_addr;
	uint32_t bl_config, erase_conf, ti_options, tap_dap_0, tap_dap_1;
	uint32_t image_valid, sp, pc;
	int keep_ccfg = 0, verbose = 0, want_hex = 0, warnings = 0, launches;
	int c;
	FILE *out;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	while ((c = getopt(argc, argv, "kvx")) != -1) {
		switch (c) {
		case 'k':
			keep_ccfg = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'x':
			want_hex = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 2 || argc > 3)
		usage();

	ware_path = argv[0];
	boot_path = argv[1];
	out_path = argc > 2 ? argv[2] : NULL;

	if (out_path != NULL) {
		const char *dot = strrchr(out_path, '.');

		if (dot != NULL && strcmp(dot, ".hex") == 0)
			want_hex = 1;
	}

	ware = read_file(ware_path, &ware_len);
	boot = read_file(boot_path, &boot_len);

	/* Be forgiving about the order of the two input files: swap them if
	 * the pair only makes sense the other way round. */
	if ((!is_oad_image(ware, ware_len) || bleboot_page(boot, boot_len) == NULL) &&
	    is_oad_image(boot, boot_len) && bleboot_page(ware, ware_len) != NULL) {
		const char *tmp_path = ware_path;
		uint8_t *tmp = ware;
		size_t tmp_len = ware_len;

		ware = boot;
		ware_len = boot_len;
		ware_path = boot_path;
		boot = tmp;
		boot_len = tmp_len;
		boot_path = tmp_path;
		printf("note    : arguments swapped - %s is the bleware\n", ware_path);
	}

	if (!is_oad_image(ware, ware_len)) {
		fprintf(stderr, "%s: %s: not a TI OAD image (no \"%s\" magic)\n",
			progname, ware_path, OAD_MAGIC);
		exit(1);
	}

	boot_page = bleboot_page(boot, boot_len);
	if (boot_page == NULL) {
		fprintf(stderr, "%s: %s: %zu bytes, expected an 8192-byte boot loader "
			"page or a %u-byte full flash dump\n",
			progname, boot_path, boot_len, FLASH_SIZE);
		exit(1);
	}

	/* ---- bleware ---- */

	img_len = rd32(ware + OAD_OFF_LEN);
	img_crc = rd32(ware + OAD_OFF_CRC32);
	entry = rd32(ware + OAD_OFF_PRGENTRY);
	end_addr = rd32(ware + OAD_OFF_ENDADDR);

	if (img_len < OAD_HDR_LEN || img_len > ware_len) {
		fprintf(stderr, "%s: %s: header length 0x%x does not fit the file "
			"(0x%zx) - truncated or corrupt image\n",
			progname, ware_path, img_len, ware_len);
		exit(1);
	}
	if (img_len > BLEBOOT_BASE) {
		fprintf(stderr, "%s: %s: image is 0x%x bytes and would overlap the "
			"boot loader at 0x%05x\n",
			progname, ware_path, img_len, BLEBOOT_BASE);
		exit(1);
	}
	if (!oad_image_address(ware, img_len, &load_addr)) {
		printf("warning : %s: no contiguous-image segment in the OAD header, "
			"assuming load address 0x00000000\n", ware_path);
		load_addr = BLEWARE_BASE;
		warnings++;
	}
	if (load_addr != BLEWARE_BASE) {
		fprintf(stderr, "%s: %s: image segment loads at 0x%08x, not 0x%05x - "
			"this is not a bleware slot image\n",
			progname, ware_path, load_addr, BLEWARE_BASE);
		exit(1);
	}

	calc_crc = crc32(0L, ware + OAD_CRC_SKIP, img_len - OAD_CRC_SKIP);

	printf("bleware : %s\n", ware_path);
	printf("          OAD NVM1, %u B (0x%x), version %u.%02u.%02u, image type 0x%02x\n",
		img_len, img_len, ware[OAD_OFF_SOFTVER + 1], ware[OAD_OFF_SOFTVER + 2],
		ware[OAD_OFF_SOFTVER + 3], ware[OAD_OFF_IMGTYPE]);
	printf("          crc32 0x%08x over [0x0c..0x%x)  %s\n",
		img_crc, img_len, img_crc == calc_crc ? "ok" : "MISMATCH");
	printf("          flash 0x%08x..0x%08x, entry 0x%08x\n",
		BLEWARE_BASE, BLEWARE_BASE + img_len - 1, entry);

	if (img_crc != calc_crc) {
		printf("warning : computed crc32 is 0x%08x - the image body does not "
			"match its header\n", calc_crc);
		printf("          (the BIM's quick scan does not re-check the crc, so it "
			"still boots, but\n");
		printf("           an OTA update of this image would be rejected)\n");
		warnings++;
	}
	if (img_len != ware_len)
		printf("note    : %zu trailing byte(s) past the header length are not "
			"copied\n", ware_len - img_len);
	if (end_addr != img_len - 1)
		printf("note    : header end address is 0x%08x, image length implies "
			"0x%08x\n", end_addr, img_len - 1);

	/* The BIM's quick scan launches the first internal-flash slot whose
	 * header passes these four tests - this is the path that boots a
	 * freshly programmed chip, before any external flash is involved. */
	launches = ware[OAD_OFF_BIMVER] == 3 && ware[OAD_OFF_METAVER] == 1 &&
		(ware[OAD_OFF_IMGTYPE] == 1 || ware[OAD_OFF_IMGTYPE] == 3 ||
		 ware[OAD_OFF_IMGTYPE] == 7) &&
		ware[OAD_OFF_CRCSTAT] != 0xfc;

	printf("          bim %u, meta %u, copy status 0x%02x, crc status 0x%02x -> %s\n",
		ware[OAD_OFF_BIMVER], ware[OAD_OFF_METAVER], ware[OAD_OFF_IMGCPSTAT],
		ware[OAD_OFF_CRCSTAT],
		launches ? "boot loader launches this image" :
			   "BOOT LOADER SKIPS THIS IMAGE");
	if (!launches) {
		printf("warning : the boot loader's quick scan rejects this header "
			"(it wants bim 3,\n");
		printf("          meta 1, image type 1/3/7 and a crc status other "
			"than 0xfc)\n");
		warnings++;
	}

	/* ---- bleboot ---- */

	sp = rd32(boot_page);
	pc = rd32(boot_page + 4);
	image_valid = rd32(boot_page + (CCFG_IMAGE_VALID_CONF - BLEBOOT_BASE));
	bl_config = rd32(boot_page + (CCFG_BL_CONFIG - BLEBOOT_BASE));
	erase_conf = rd32(boot_page + (CCFG_ERASE_CONF - BLEBOOT_BASE));
	ti_options = rd32(boot_page + (CCFG_TI_OPTIONS - BLEBOOT_BASE));
	tap_dap_0 = rd32(boot_page + (CCFG_TAP_DAP_0 - BLEBOOT_BASE));
	tap_dap_1 = rd32(boot_page + (CCFG_TAP_DAP_1 - BLEBOOT_BASE));

	printf("bleboot : %s\n", boot_path);
	if (boot_len == FLASH_SIZE)
		printf("          full flash dump, using the last page\n");
	printf("          %u B, flash 0x%08x..0x%08x, sp 0x%08x, reset 0x%08x\n",
		PAGE_SIZE, BLEBOOT_BASE, BLEBOOT_BASE + PAGE_SIZE - 1, sp, pc);
	if (memcmp(boot_page + (BVER_BASE - BLEBOOT_BASE), "BVER", 4) == 0)
		printf("          version \"%.11s %.8s\"\n",
			boot_page + (BVER_BASE - BLEBOOT_BASE) + 4,
			boot_page + (BVER_TIME - BLEBOOT_BASE));
	if ((sp & 0xfff00000u) != 0x20000000u || pc < BLEBOOT_BASE ||
	    pc >= BLEBOOT_BASE + PAGE_SIZE) {
		printf("warning : the page does not start with a plausible CC2642R1F "
			"vector table\n");
		warnings++;
	}
	if (image_valid != BLEBOOT_BASE) {
		printf("warning : IMAGE_VALID_CONF is 0x%08x, the ROM will not boot "
			"0x%05x\n", image_valid, BLEBOOT_BASE);
		warnings++;
	}

	/* ---- CCFG ---- */

	printf("ccfg    : CCFG_BL_CONFIG        0x%08x  rom serial boot loader %s\n",
		bl_config, ((bl_config >> 24) & 0xff) == TAP_ENABLE ? "enabled" : "disabled");
	printf("          CCFG_ERASE_CONF       0x%08x  chip erase %s, bank erase %s\n",
		erase_conf, (erase_conf >> 8) & 1 ? "enabled" : "DISABLED",
		erase_conf & 1 ? "enabled" : "DISABLED");
	printf("          CCFG_TI_OPTIONS       0x%08x  ti failure analysis %s\n",
		ti_options, tap_state(ti_options, 0));
	printf("          CCFG_TAP_DAP_0        0x%08x  cpu dap %s, pwrprof tap %s, test tap %s\n",
		tap_dap_0, tap_state(tap_dap_0, 16), tap_state(tap_dap_0, 8),
		tap_state(tap_dap_0, 0));
	printf("          CCFG_TAP_DAP_1        0x%08x  pbist2 tap %s, pbist1 tap %s, aon tap %s\n",
		tap_dap_1, tap_state(tap_dap_1, 16), tap_state(tap_dap_1, 8),
		tap_state(tap_dap_1, 0));
	printf("          CCFG_IMAGE_VALID_CONF 0x%08x  rom boots this address\n",
		image_valid);
	printf("          => jtag/xds110 is %s in the input image\n",
		((tap_dap_0 >> 16) & 0xff) == TAP_ENABLE ? "usable" : "locked out");
	if (verbose)
		dump_ccfg(boot_page);

	/* ---- merge ---- */

	image = malloc(FLASH_SIZE);
	if (image == NULL) {
		fprintf(stderr, "%s: malloc(%u): Out of memory\n", progname, FLASH_SIZE);
		exit(1);
	}
	memset(image, 0xff, FLASH_SIZE);
	memcpy(image + BLEWARE_BASE, ware, img_len);
	memcpy(image + BLEBOOT_BASE, boot_page, PAGE_SIZE);

	if (keep_ccfg) {
		printf("ccfg    : left untouched (-k)\n");
		if (((tap_dap_0 >> 16) & 0xff) != TAP_ENABLE) {
			printf("warning : the merged image locks the debug port again on "
				"the first boot\n");
			warnings++;
		}
	} else {
		wr32(image + CCFG_TI_OPTIONS,
			(ti_options & ~0xffu) | (TI_OPTIONS_OPEN & 0xffu));
		wr32(image + CCFG_TAP_DAP_0,
			(tap_dap_0 & 0xff000000u) | (TAP_DAP_OPEN & 0x00ffffffu));
		wr32(image + CCFG_TAP_DAP_1,
			(tap_dap_1 & 0xff000000u) | (TAP_DAP_OPEN & 0x00ffffffu));

		printf("unlock  : CCFG_TI_OPTIONS       0x%08x -> 0x%08x\n",
			ti_options, rd32(image + CCFG_TI_OPTIONS));
		printf("          CCFG_TAP_DAP_0        0x%08x -> 0x%08x\n",
			tap_dap_0, rd32(image + CCFG_TAP_DAP_0));
		printf("          CCFG_TAP_DAP_1        0x%08x -> 0x%08x\n",
			tap_dap_1, rd32(image + CCFG_TAP_DAP_1));
		printf("          => jtag/xds110 stays usable after flashing\n");
	}

	/* ---- output ---- */

	if (out_path == NULL) {
		printf("output  : none - pass an output file to write the merged image\n");
		if (warnings)
			printf("done    : %d warning(s)\n", warnings);
		return 0;
	}

	out = fopen(out_path, want_hex ? "w" : "wb");
	if (out == NULL) {
		fprintf(stderr, "%s: fopen(%s): %s\n", progname, out_path, strerror(errno));
		exit(1);
	}

	if (want_hex) {
		hex_region(out, image, BLEWARE_BASE, BLEWARE_BASE + img_len);
		hex_region(out, image, BLEBOOT_BASE, FLASH_SIZE);
		hex_record(out, 0x01, 0, NULL, 0);
		printf("output  : %s, intel hex, 0x%08x..0x%08x + 0x%08x..0x%08x\n",
			out_path, BLEWARE_BASE, BLEWARE_BASE + img_len - 1,
			BLEBOOT_BASE, FLASH_SIZE - 1);
	} else {
		if (fwrite(image, 1, FLASH_SIZE, out) != FLASH_SIZE) {
			fprintf(stderr, "%s: fwrite(%s): %s\n", progname, out_path,
				strerror(errno));
			exit(1);
		}
		printf("output  : %s, raw binary, %u B (0x%x), load at 0x00000000\n",
			out_path, FLASH_SIZE, FLASH_SIZE);
	}

	if (fclose(out) != 0) {
		fprintf(stderr, "%s: fclose(%s): %s\n", progname, out_path, strerror(errno));
		exit(1);
	}

	if (warnings)
		printf("done    : %d warning(s)\n", warnings);

	return 0;
}
