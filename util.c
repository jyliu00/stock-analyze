#include "util.h"

#include <string.h>

uint32_t sr_height_margin = 80; /* support/resist height: 8% */
uint32_t spt_pullback_margin = 60; /* 7.5% pullback */
uint32_t bo_sr_height_margin = 50;

void strlcpy(char *dest, const char *src, int dest_sz)
{
	strncpy(dest, src, dest_sz - 1);
	dest[dest_sz - 1] = 0;
}
