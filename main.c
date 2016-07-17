#include "util.h"
#include "fetch_price.h"
#include "stock_price.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>

static char ticker_list_fname[128];

enum
{
	ACTION_NONE,

	ACTION_FETCH,
	ACTION_CHECK_SPT, /* support */
	ACTION_CHECK_DB, /* double bottom */
	ACTION_CHECK_PB, /* pull back */
	ACTION_CHECK_LOW_VOLUME,

	ACTION_NR
};

static void print_usage(void)
{
	printf("Usage: anna -country={usa|china|canada|biotech|ibd} [-date=yyyy-mm-dd] [-conf=filename]\n");
	printf("               {fetch | check-spt | check-db | check-pb | check-low-volume} [symbol-1 symbol-2 ...]\n");
}

static int init_dirs(const char *country)
{
	char path[128];

	if (access(ROOT_DIR, F_OK) < 0) {
		if (mkdir(ROOT_DIR, 0777) < 0) {
			anna_error("mkdir(%s) failed: %d(%s)\n", ROOT_DIR, errno, strerror(errno));
			return -1;
		}
	}

	if (access(ROOT_DIR_TMP, F_OK) < 0) {
		if (mkdir(ROOT_DIR_TMP, 0777) < 0) {
			anna_error("mkdir(%s) failed: %d(%s)\n", ROOT_DIR_TMP, errno, strerror(errno));
			return -1;
		}
	}

	snprintf(path, sizeof(path), ROOT_DIR "/%s", country);
	if (access(path, F_OK) < 0) {
		if (mkdir(path, 0777) < 0) {
			anna_error("mkdir(%s) failed: %d(%s)\n", path, errno, strerror(errno));
			return -1;
		}
	}

	return 0;
}

static int load_config_file(const char *fname, const char *country)
{
	char buf[512];
	int country_matched = 0;

	if (!fname || !fname[0])
		fname = "anna.conf";

	FILE *fp = fopen(fname, "r");
	if (!fp) {
		anna_error("fopen(%s) failed: %d(%s)\n", fname, errno, strerror(errno));
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		char *p;

		if (buf[0] == '#' || buf[0] == '\n')
			continue;

		buf[strlen(buf) - 1] = 0;

		if (buf[0] == '[' && strncmp(buf, "[country=", strlen("[country=")) == 0) {
			p = strchr(buf, '=');

			if (strncmp(p + 1, country, strlen(country)) == 0)
				country_matched = 1;
			else
				country_matched = 0;

			continue;
		}

		if (country_matched) {
			if (strncmp(buf, "ticker_list_file=", strlen("ticker_list_file=")) == 0) {
				p = strchr(buf, '=');
				strlcpy(ticker_list_fname, p + 1, sizeof(ticker_list_fname));
			}
			else if (strncmp(buf, "sr_height_margin=", strlen("sr_height_margin=")) == 0) {
				p = strchr(buf, '=');
				sr_height_margin = atoi(p + 1);
			}
		}
	}

	fclose(fp);

	return 0;
}

int main(int argc, const char **argv)
{
	char country[16] = { 0 };
	char date[12] = { 0 };
	char conf_fname[64] = { 0 };
	int action = ACTION_NONE;
	char *symbols[256] = { NULL };
	int symbols_nr = 0;
	char *p;
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (arg[0] != '-') {
			if (isupper(arg[0]) || isdigit(arg[0])) {
				symbols[symbols_nr++] = strdup(arg);
			}
			else if (strcmp(arg, "fetch") == 0) {
				action = ACTION_FETCH;
			}
			else if (strcmp(arg, "check-spt") == 0) {
				action = ACTION_CHECK_SPT;
			}
			else if (strcmp(arg, "check-db") == 0) {
				action = ACTION_CHECK_DB;
			}
			else if (strcmp(arg, "check-pb") == 0) {
				action = ACTION_CHECK_PB;
			}
			else if (strcmp(arg, "check-low-volume") == 0) {
				action = ACTION_CHECK_LOW_VOLUME;
			}
		}
		else if (strncmp(arg, "-country=", strlen("-country=")) == 0) {
			p = strchr(arg, '=');
			strlcpy(country, p + 1, sizeof(country));
		}
		else if (strncmp(arg, "-date=", strlen("-date=")) == 0) {
			p = strchr(arg, '=');
			strlcpy(date, p + 1, sizeof(date));
		}
		else if (strcmp(arg, "-realtime") == 0) {
			strlcpy(date, arg + 1,sizeof(date));
		}
		else if (strncmp(arg, "-conf=", strlen("-conf=")) == 0) {
			p = strchr(arg, '=');
			strlcpy(conf_fname, p + 1, sizeof(conf_fname));
		}
	}

	if (action == ACTION_NONE || country[0] == 0
	    || (strcmp(country, "usa") && strcmp(country, "china")
		&& strcmp(country, "canada") && strcmp(country, "biotech")
		&& strcmp(country, "ibd")))
	{
		print_usage( );
		goto finish;
	}

	if (init_dirs(country) < 0)
		goto finish;

	if (load_config_file(conf_fname, country) < 0)
		goto finish;

	switch (action) {
	case ACTION_FETCH:
		fetch_symbols_price(country, ticker_list_fname, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SPT:
		stock_price_check_support(country, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_DB:
		stock_price_check_doublebottom(country, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_PB:
		stock_price_check_pullback(country, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_LOW_VOLUME:
		stock_price_check_low_volume(country, date, symbols_nr, (const char **)symbols);
		break;
	}

finish:
	for (i = 0; i < symbols_nr; i++) {
		if (symbols[i])
			free(symbols[i]);
	}

	return 0;
}
