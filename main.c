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
	ACTION_FETCH_REALTIME,
	ACTION_CHECK_SPT, /* support */
	ACTION_CHECK_SMA20d, /* support at sma20d */
	ACTION_CHECK_SMA50d, /* support at sma50d */
	ACTION_CHECK_DB, /* double bottom */
	ACTION_CHECK_PB, /* pull back */
	ACTION_CHECK_BO, /* break out */
	ACTION_CHECK_52W_LOWUP, /* up from 52w low */
	ACTION_CHECK_WUP, /* week up */
	ACTION_CHECK_WRV, /* week reverse */
	ACTION_CHECK_LOW_VOLUME,
	ACTION_CHECK_LOW_VOLUME_UP,
	ACTION_CHECK_VOLUME_UP,
	ACTION_CHECK_CHANGE,

	ACTION_NR
};

const char *group_list[ ] = { "usa", "ibd", "biotech", "3x", "china", "canada", NULL };

static void print_usage(void)
{
	printf("Usage: anna -group={usa|china|canada|biotech|ibd|3x} [-date=yyyy-mm-dd] [-conf=filename]\n");
	printf("               {fetch | fetch-rt | check-db | check-lvup | check-52wlup | check-spt | check-20d | check-50d | check-pb | check-bo | check-wup | check-wrv | check-lv | check-chg} [symbol-1 symbol-2 ...]\n");
}

static int init_dirs(const char *group)
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

	snprintf(path, sizeof(path), ROOT_DIR "/%s", group);
	if (access(path, F_OK) < 0) {
		if (mkdir(path, 0777) < 0) {
			anna_error("mkdir(%s) failed: %d(%s)\n", path, errno, strerror(errno));
			return -1;
		}
	}

	return 0;
}

static int load_config_file(const char *fname, const char *group)
{
	char buf[512];
	int group_matched = 0;

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

		if (buf[0] == '[' && strncmp(buf, "[group=", strlen("[group=")) == 0) {
			p = strchr(buf, '=');

			if (strncmp(p + 1, group, strlen(group)) == 0)
				group_matched = 1;
			else
				group_matched = 0;

			continue;
		}

		if (group_matched) {
			if (strncmp(buf, "ticker_list_file=", strlen("ticker_list_file=")) == 0) {
				p = strchr(buf, '=');
				strlcpy(ticker_list_fname, p + 1, sizeof(ticker_list_fname));
			}
			else if (strncmp(buf, "sr_height_margin=", strlen("sr_height_margin=")) == 0) {
				p = strchr(buf, '=');
				sr_height_margin = atoi(p + 1);
			}
			else if (strncmp(buf, "spt_pullback_margin=", strlen("spt_pullback_margin=")) == 0) {
				p = strchr(buf, '=');
				spt_pullback_margin = atoi(p + 1);
			}
		}
	}

	fclose(fp);

	return 0;
}

int main(int argc, const char **argv)
{
	char group[16] = { 0 };
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
			else if (strcmp(arg, "fetch-rt") == 0) {
				action = ACTION_FETCH_REALTIME;
			}
			else if (strcmp(arg, "check-spt") == 0) {
				action = ACTION_CHECK_SPT;
			}
			else if (strcmp(arg, "check-20d") == 0) {
				action = ACTION_CHECK_SMA20d;
			}
			else if (strcmp(arg, "check-50d") == 0) {
				action = ACTION_CHECK_SMA50d;
			}
			else if (strcmp(arg, "check-db") == 0) {
				action = ACTION_CHECK_DB;
			}
			else if (strcmp(arg, "check-pb") == 0) {
				action = ACTION_CHECK_PB;
			}
			else if (strcmp(arg, "check-52wlup") == 0) {
				action = ACTION_CHECK_52W_LOWUP;
			}
			else if (strcmp(arg, "check-bo") == 0) {
				action = ACTION_CHECK_BO;
			}
			else if (strcmp(arg, "check-wup") == 0) {
				action = ACTION_CHECK_WUP;
			}
			else if (strcmp(arg, "check-wrv") == 0) {
				action = ACTION_CHECK_WRV;
			}
			else if (strcmp(arg, "check-lv") == 0) {
				action = ACTION_CHECK_LOW_VOLUME;
			}
			else if (strcmp(arg, "check-lvup") == 0) {
				action = ACTION_CHECK_LOW_VOLUME_UP;
			}
			else if (strcmp(arg, "check-vup") == 0) {
				action = ACTION_CHECK_VOLUME_UP;
			}
			else if (strcmp(arg, "check-chg") == 0) {
				action = ACTION_CHECK_CHANGE;
			}
		}
		else if (strncmp(arg, "-group=", strlen("-group=")) == 0) {
			p = strchr(arg, '=');
			strlcpy(group, p + 1, sizeof(group));
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

	if (action == ACTION_NONE || group[0] == 0)
	{
		print_usage( );
		goto finish;
	}

	if (group[0]) {
		for (i = 0; group_list[i]; i++) {
			if (strcmp(group, group_list[i]) == 0)
				break;
		}

		if (group_list[i] == NULL) {
			print_usage( );
			goto finish;
		}
	}

	if (init_dirs(group) < 0)
			goto finish;

	if (load_config_file(conf_fname, group) < 0)
		goto finish;

	switch (action) {
	case ACTION_FETCH:
		fetch_symbols_price(0, group, ticker_list_fname, symbols_nr, (const char **)symbols);
		break;

	case ACTION_FETCH_REALTIME:
		fetch_symbols_price(1, group, ticker_list_fname, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SPT:
		stock_price_check_support(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA20d:
		stock_price_check_sma(group, date, SMA_20d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA50d:
		stock_price_check_sma(group, date, SMA_50d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_DB:
		stock_price_check_doublebottom(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_PB:
		stock_price_check_pullback(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_BO:
		stock_price_check_breakout(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_52W_LOWUP:
                stock_price_check_52w_low_up(group, date, symbols_nr, (const char **)symbols);
                break;

	case ACTION_CHECK_WUP:
		stock_price_check_weekup(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_WRV:
		stock_price_check_week_reverse(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_LOW_VOLUME:
		stock_price_check_low_volume(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_LOW_VOLUME_UP:
		stock_price_check_low_volume_up(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_VOLUME_UP:
		stock_price_check_volume_up(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_CHANGE:
		stock_price_check_change(group, date, symbols_nr, (const char **)symbols);
		break;
	}

finish:
	for (i = 0; i < symbols_nr; i++) {
		if (symbols[i])
			free(symbols[i]);
	}

	return 0;
}
