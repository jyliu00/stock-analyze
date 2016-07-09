#include "util.h"
#include "fetch_price.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

static void print_usage(void)
{
	printf("Usage: anna -country={usa|china|canada} [-date=yyyy-mm-dd] [-file=filename]\n");
	printf("               [-fetch | -check-support] [symbol-1 symbol-2 ...]\n");
}

int main(int argc, const char **argv)
{
	char country[16] = { 0 };
	char date[12] = { 0 };
	char filename[64] = { 0 };
	int action = ACTION_NONE;
	char *symbols[256] = { NULL };
	int symbols_nr = 0;
	char *p;
	int i;

	for (i = 1; i < argc; i++) {
		if (strncmp(argv[i], "-country=", strlen("-country=")) == 0) {
			p = strchr(argv[i], '=');
			strlcpy(country, p + 1, sizeof(country));
		}
		else if (strncmp(argv[i], "-date=", strlen("-date=")) == 0) {
			p = strchr(argv[i], '=');
			strlcpy(date, p + 1, sizeof(date));
		}
		else if (strncmp(argv[i], "-file=", strlen("-file=")) == 0) {
			p = strchr(argv[i], '=');
			strlcpy(filename, p + 1, sizeof(filename));
		}
		else if (strcmp(argv[i], "-fetch") == 0) {
			action = ACTION_FETCH;
		}
		else if (strcmp(argv[i], "-check-support") == 0) {
			action = ACTION_CHECK_SUPPORT;
		}
		else {
			symbols[symbols_nr++] = strdup(argv[i]);
		}
	}

	if (action == ACTION_NONE || country[0] == 0
	    || (strcmp(country, "usa") && strcmp(country, "china") && strcmp(country, "canada")))
	{
		print_usage( );
		goto finish;
	}

	if (filename[0] && access(filename, F_OK) < 0) {
		anna_error("access(%s) failed: %d(%s)\n", filename, errno, strerror(errno));
		goto finish;
	}

finish:
	for (i = 0; i < symbols_nr; i++) {
		if (symbols[i])
			free(symbols[i]);
	}

	return 0;
}
