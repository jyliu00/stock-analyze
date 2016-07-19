#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>

#define anna_error(fmt, args...) \
	fprintf(stderr, "[%s:%s:%d] " fmt, __FILE__, __FUNCTION__, __LINE__, ##args)

#define anna_info(fmt, args...) \
	do { \
		fprintf(stdout, fmt, ##args); \
		fflush(stdout); \
	} while (0)

#define anna_debug(fmt, args...) \
	do { \
		if (1) { \
			fprintf(stdout, fmt, ##args); \
			fflush(stdout); \
		} \
	} while (0)

void strlcpy(char *dest, const char *src, int dest_sz);

#define ROOT_DIR      "/dev/shm/anna"
#define ROOT_DIR_TMP  ROOT_DIR "/tmp"

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

extern uint32_t sr_height_margin;
extern uint32_t spt_pullback_margin;
extern uint32_t bo_sr_height_margin;

#endif /* __UTIL_H__ */
