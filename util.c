#include "util.h"

#include <string.h>

uint32_t sr_height_margin = 80; /* 8% height */

void strlcpy(char *dest, const char *src, int dest_sz)
{
	strncpy(dest, src, dest_sz - 1);
	dest[dest_sz - 1] = 0;
}
