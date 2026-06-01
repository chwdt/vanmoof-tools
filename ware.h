#ifndef _WARE_H
#define _WARE_H 1

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

#define MAINWARE_OFFSET 0x08020000

typedef struct {
	uint32_t magic;
	uint8_t version[4];
	uint32_t crc;
	uint32_t length;
	char date[12];
	char time[12];
} vanmoof_ware_t;

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
