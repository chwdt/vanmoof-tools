#ifndef _WARE_H
#define _WARE_H 1

#include <stdint.h>

#define HEAD_MAGIC 0x96f3b83d

typedef struct {
        uint32_t magic;
        uint32_t unknown0;
        uint32_t offset;
	uint32_t length;
	uint32_t unknown1;
	uint32_t version0;
	uint32_t version1;
	uint32_t unknown2;
} vanmoof_head_t;

#define WARE_MAGIC 0xaa55aa55

enum WARE_TYPE {
	MOTOR = 0xa1,
	BATTERY = 0xb1,
	POWERBANK = 0xb2,
	SHIFTER = 0xc1,
	MAIN = 0xf4,
};

static inline const char *ware_type_name(uint8_t type)
{
	switch (type) {
		case MOTOR:     return "MOTOR";
		case BATTERY:   return "BATTERY";
		case POWERBANK: return "POWERBANK";
		case SHIFTER:   return "SHIFTER";
		case MAIN:      return "MAIN";
		default:        return "UNKNOWN";
	}
}

#define MAINWARE_OFFSET 0x08020000

/* version[0] is the WARE_TYPE; version[1..3] are patch/minor/major encoded
 * as BCD (e.g. 0x17 == "17"), so they must be printed as hex, not decimal:
 * 0x011701b1 reads as type 0xb1, version 1.17.1 (not 1.23.1). */
typedef struct {
	uint32_t magic;
	uint8_t version[4];
	uint32_t crc;
	uint32_t length;
	char date[12];
	char time[12];
} vanmoof_ware_t;

/*
 * VanMoof S5/A5 Cortex-M ECU image (user_ecu, imx8_bridge, elock, eshifter,
 * frontlight, rearlight, motor_sensor, power_control, power_pedal). The whole
 * .bin is the flashable image; a "VMFW" header sits at a fixed offset (0x134,
 * just past the Cortex-M vector table) rather than at the start of the file.
 *
 * The CRC is the standard CRC-32 (zlib / Ethernet poly 0x04c11db7, reflected,
 * init and final-xor 0xffffffff) over the whole image with the crc and length
 * header fields both set to 0xffffffff -- the same field treatment as
 * vanmoof_ware_t, but the modern reflected CRC instead of the STM32 one.
 *
 * version is packed exactly like manifest.txt (devices/main/update/src/
 * manifest.c): major<<24 | minor<<16 | (variant & 7)<<13 | (patch & 0x1fff).
 * 0x01056000 reads as 1.5.0 main (variant 3 == main).
 */
#define VMFW_MAGIC "VMFW"
#define VMFW_OFFSET 0x134

typedef struct {
	char     magic[4];	/* "VMFW" */
	uint32_t version;	/* packed semver, see vmfw_version_* below */
	uint32_t crc;		/* CRC-32 over image, crc+length set to 0xffffffff */
	uint32_t length;	/* total image length == file size */
	char     date[12];	/* __DATE__, e.g. "Jan 29 2024" */
	char     time[12];	/* __TIME__, e.g. "14:50:32" (9 bytes used) */
} vmfw_ware_t;

#define vmfw_version_major(v)	(((v) >> 24) & 0xff)
#define vmfw_version_minor(v)	(((v) >> 16) & 0xff)
#define vmfw_version_variant(v)	(((v) >> 13) & 0x7)
#define vmfw_version_patch(v)	((v) & 0x1fff)

/* variant code as packed by the manifest parser (manifest_variant_code) */
static inline const char *vmfw_variant_name(uint32_t variant)
{
	switch (variant) {
		case 1:  return "dev";
		case 2:  return "rc";
		case 3:  return "main";
		default: return "untagged";
	}
}

#define BLE_WARE_MAGIC "OAD NVM1"
#define BLE_WARE_OFFSET 0x00000000

typedef struct {
	uint8_t	magic[8];
	uint32_t crc;
	uint8_t meta_ver;
	uint8_t bim_ver;
	uint16_t tech_type;
	uint8_t crc_stat;
	uint8_t img_cp_stat;
	uint8_t img_no;
	uint8_t img_type;
	uint32_t img_vld;
	uint32_t len;
	uint32_t prg_entry;
	uint32_t soft_ver;
	uint32_t img_end_addr;
	uint16_t hdr_len;
	uint16_t rfu;
} ble_ware_t;

typedef struct __attribute__((packed)) {
	uint8_t seg_type;
	uint16_t wireless_tech;
	uint8_t rfu;
	uint32_t seg_len;
} ble_ware_seg_t;

#define BLE_SEG_TYPE_BOUNDARY 0
#define BLE_SEG_TYPE_CONTIGUOUS 1
#define BLE_SEG_TYPE_NONCONTIGUOUS 2
#define BLE_SEG_TYPE_SECURITY 3
#define BLE_SEG_TYPE_NVRAM 4
#define BLE_SEG_TYPE_DELTA 5

typedef struct __attribute__((packed)) {
	uint8_t sig_ver;
	uint32_t timestamp;
	uint8_t ecdsa_signer[8];
	uint8_t ecdsa_signature[64];
} ble_ware_signature_seg_t;

typedef struct {
	uint32_t start_addr;
} ble_ware_code_seg_t;

#define IMAGE_TLV_INFO_MAGIC	0x6907
#define IMAGE_TLV_SHA256	0x0010
#define IMAGE_TLV_KEYHASH	0x0001
#define IMAGE_TLV_ECDSA_SIG	0x0022

typedef struct {
	uint16_t type;
	uint16_t length;
	uint8_t data[0];
} image_tlv_t;

#endif
