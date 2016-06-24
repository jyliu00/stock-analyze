#ifndef __UTIL_H__
#define __UTIL_H__

#define anna_error(fmt, args...) \
	fprintf(stderr, "[%s:%s:%d] " fmt, __FILE__, __FUNCTION__, __LINE__, ##args)

void strlcpy(char *dest, const char *src, int dest_sz);

#endif /* __UTIL_H__ */
