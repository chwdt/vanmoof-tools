#ifndef _WARE_H
#define _WARE_H 1

#define WARE_MAGIC 0xaa55aa55

typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t crc;
	uint32_t length;
	char date[12];
	char time[12];
} vanmoof_ware_t;

#endif
