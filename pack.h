#ifndef _PACK_H
#define _PACK_H 1

#include <stdint.h>

#define PACK_MAGIC	"PACK"

typedef struct {
	char magic[4];
	uint32_t offset;
	uint32_t length;
} pack_header_t;

typedef struct {
	char filename[56];
	uint32_t offset;
	uint32_t length;
} pack_entry_t;

#endif
