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
	ACTION_CHECK_SMA30d, /* support at sma30d */
	ACTION_CHECK_SMA50d, /* support at sma50d */
	ACTION_CHECK_SMA60d, /* support at sma60d */
	ACTION_CHECK_26W_LOW_SMA20d, /* 26 week low cross above sma20 */
	ACTION_CHECK_26W_LOW_SMA50d, /* 26 week low cross above sma50 */
	ACTION_CHECK_13W_LOW_SMA20d, /* 13 week low cross above sma20 */
	ACTION_CHECK_13W_LOW_SMA50d, /* 13 week low cross above sma50 */
	ACTION_CHECK_SMA10d_UP, /* cross above sma20 */
	ACTION_CHECK_SMA20d_UP, /* cross above sma20 */
	ACTION_CHECK_SMA50d_UP, /* cross above sma50 */
	ACTION_CHECK_SMA200d_UP, /* cross above sma200 */
	ACTION_CHECK_STRONG_SMA20d_UP,
	ACTION_CHECK_SMA20d_PULLBACK, /* previous day cross above sma20d, today pullback beneth sma20d */
	ACTION_CHECK_SMA50d_PULLBACK, /* previous day cross above sma50d, today pullback beneth sma50d */
	ACTION_CHECK_SMA10d_BREAKOUT, /* breakout sma10d */
	ACTION_CHECK_SMA20d_BREAKOUT, /* breakout sma20d */
	ACTION_CHECK_SMA10d_TRENDUP,
	ACTION_CHECK_DB, /* double bottom */
	ACTION_CHECK_PULLBACK_DB, /* pullback double bottom */
	ACTION_CHECK_MFI_DB, /* double bottom with rising money flow index */
	ACTION_CHECK_52W_DB, /* 52w low double bottom */
	ACTION_CHECK_52W_DBUP, /* 52w low double bottom up */
	ACTION_CHECK_DBUP, /* up from double bottom */
	ACTION_CHECK_PULLBACK_DBUP, /* up from pullback double bottom */
	ACTION_CHECK_STRONG_DBUP, /* strong up from double bottom */
	ACTION_CHECK_PB, /* pull back */
	ACTION_CHECK_BO, /* break out */
	ACTION_CHECK_2nd_BO, /* 2nd break out */
	ACTION_CHECK_52W_LOWUP, /* up from 52w low */
	ACTION_CHECK_CHANGE,
	ACTION_CHECK_TREND_BO, /* trendline breakout */
	ACTION_CHECK_STRONG_UPTREND,
	ACTION_CHECK_STRONG_BO,
	ACTION_CHECK_STRONG_BODY_BO,
	ACTION_CHECK_RESIST_BO,
	ACTION_CHECK_MFI,
	ACTION_CHECK_REVERSE_UP,
	ACTION_CHECK_HIGHER_LOW,

	ACTION_NR
};

const char *group_list[ ] = { "usa", "iwm", "mdy", "zacks", "ibd", "biotech", "3x", "china", "canada", NULL };

static void print_usage(void)
{
	printf("Usage: anna -group={usa|china|canada|iwm|mdy|biotech|zacks|ibd|3x} [-date=yyyy-mm-dd] [-conf=filename]\n");
	printf("               {fetch | fetch-rt | check-db | check-mfi-db | check-pullback-db | check-52w-db | "
				"check-dbup | check-pullback-dbup | check-52w-dbup | check-strong-dbup | check-52wlup | check-higher-low"
				"check-spt | check-20d | check-30d | check-50d | check-60d | check-20dlow | check-50dlow | check-26w20dlow | check-26w50dlow | "
				"check-10dup | check-20dup | check-strong-20dup | check-50dup | check-200dup | check-20dpb | check-50dpb | check-pb | check-bo | check-2ndbo | "
				"check-trend-bo | check-strong-uptrend | check-strong-bo | check-10d-trendup | check-resist-bo | check-mfi | check-reverse-upday | check-chg} [symbol-1 symbol-2 ...]\n");
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
			else if (strncmp(buf, "fetch_source=", strlen("fetch_source=")) == 0) {
				p = strchr(buf, '=');
				if (*(p + 1) == 'g')
					fetch_source = FETCH_SOURCE_GOOGLE;
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
			else if (strcmp(arg, "check-30d") == 0) {
				action = ACTION_CHECK_SMA30d;
			}
			else if (strcmp(arg, "check-50d") == 0) {
				action = ACTION_CHECK_SMA50d;
			}
			else if (strcmp(arg, "check-60d") == 0) {
				action = ACTION_CHECK_SMA60d;
			}
			else if (strcmp(arg, "check-20dlow") == 0) {
				action = ACTION_CHECK_13W_LOW_SMA20d;
			}
			else if (strcmp(arg, "check-50dlow") == 0) {
				action = ACTION_CHECK_13W_LOW_SMA50d;
			}
			else if (strcmp(arg, "check-26w20dlow") == 0) {
				action = ACTION_CHECK_26W_LOW_SMA20d;
			}
			else if (strcmp(arg, "check-26w50dlow") == 0) {
				action = ACTION_CHECK_26W_LOW_SMA50d;
			}
			else if (strcmp(arg, "check-10dup") == 0) {
				action = ACTION_CHECK_SMA10d_UP;
			}
			else if (strcmp(arg, "check-20dup") == 0) {
				action = ACTION_CHECK_SMA20d_UP;
			}
			else if (strcmp(arg, "check-strong-20dup") == 0) {
				action = ACTION_CHECK_STRONG_SMA20d_UP;
			}
			else if (strcmp(arg, "check-50dup") == 0) {
				action = ACTION_CHECK_SMA50d_UP;
			}
			else if (strcmp(arg, "check-200dup") == 0) {
				action = ACTION_CHECK_SMA200d_UP;
			}
			else if (strcmp(arg, "check-20dpb") == 0) {
				action = ACTION_CHECK_SMA20d_PULLBACK;
			}
			else if (strcmp(arg, "check-50dpb") == 0) {
				action = ACTION_CHECK_SMA50d_PULLBACK;
			}
			else if (strcmp(arg, "check-10d-bo") == 0) {
				action = ACTION_CHECK_SMA10d_BREAKOUT;
			}
			else if (strcmp(arg, "check-20d-bo") == 0) {
				action = ACTION_CHECK_SMA20d_BREAKOUT;
			}
			else if (strcmp(arg, "check-10d-trendup") == 0) {
				action = ACTION_CHECK_SMA10d_TRENDUP;
			}
			else if (strcmp(arg, "check-db") == 0) {
				action = ACTION_CHECK_DB;
			}
			else if (strcmp(arg, "check-mfi-db") == 0) {
				action = ACTION_CHECK_MFI_DB;
			}
			else if (strcmp(arg, "check-pullback-db") == 0) {
				action = ACTION_CHECK_PULLBACK_DB;
			}
			else if (strcmp(arg, "check-52w-db") == 0) {
				action = ACTION_CHECK_52W_DB;
			}
			else if (strcmp(arg, "check-52w-dbup") == 0) {
				action = ACTION_CHECK_52W_DBUP;
			}
			else if (strcmp(arg, "check-dbup") == 0) {
				action = ACTION_CHECK_DBUP;
			}
			else if (strcmp(arg, "check-pullback-dbup") == 0) {
				action = ACTION_CHECK_PULLBACK_DBUP;
			}
			else if (strcmp(arg, "check-strong-dbup") == 0) {
				action = ACTION_CHECK_STRONG_DBUP;
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
			else if (strcmp(arg, "check-2ndbo") == 0) {
				action = ACTION_CHECK_2nd_BO;
			}
			else if (strcmp(arg, "check-trend-bo") == 0) {
				action = ACTION_CHECK_TREND_BO;
			}
			else if (strcmp(arg, "check-strong-uptrend") == 0) {
				action = ACTION_CHECK_STRONG_UPTREND;
			}
			else if (strcmp(arg, "check-strong-bo") == 0) {
				action = ACTION_CHECK_STRONG_BO;
			}
			else if (strcmp(arg, "check-strong-body-bo") == 0) {
				action = ACTION_CHECK_STRONG_BODY_BO;
			}
			else if (strcmp(arg, "check-resist-bo") == 0) {
				action = ACTION_CHECK_RESIST_BO;
			}
			else if (strcmp(arg, "check-chg") == 0) {
				action = ACTION_CHECK_CHANGE;
			}
			else if (strcmp(arg, "check-mfi") == 0) {
				action = ACTION_CHECK_MFI;
			}
			else if (strcmp(arg, "check-reverse-up") == 0) {
				action = ACTION_CHECK_REVERSE_UP;
			}
			else if (strcmp(arg, "check-higher-low") == 0) {
				action = ACTION_CHECK_HIGHER_LOW;
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

	case ACTION_CHECK_SMA30d:
		stock_price_check_sma(group, date, SMA_30d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA50d:
		stock_price_check_sma(group, date, SMA_50d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA60d:
		stock_price_check_sma(group, date, SMA_60d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_26W_LOW_SMA20d:
		stock_price_check_weeks_low_sma(group, date, 26, SMA_20d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_26W_LOW_SMA50d:
		stock_price_check_weeks_low_sma(group, date, 26, SMA_50d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_13W_LOW_SMA20d:
		stock_price_check_weeks_low_sma(group, date, 13, SMA_20d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_13W_LOW_SMA50d:
		stock_price_check_weeks_low_sma(group, date, 13, SMA_50d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA10d_UP:
		stock_price_check_weeks_low_sma(group, date, 0, SMA_10d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA20d_UP:
		stock_price_check_weeks_low_sma(group, date, 0, SMA_20d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA50d_UP:
		stock_price_check_sma_up(group, date, SMA_50d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA200d_UP:
		stock_price_check_weeks_low_sma(group, date, 0, SMA_200d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA20d_PULLBACK:
		stock_price_check_sma_pullback(group, date, SMA_20d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA50d_PULLBACK:
		stock_price_check_sma_pullback(group, date, SMA_50d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA10d_BREAKOUT:
		stock_price_check_sma_breakout(group, date, SMA_10d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA20d_BREAKOUT:
		stock_price_check_sma_breakout(group, date, SMA_20d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_STRONG_SMA20d_UP:
		stock_price_check_strong_sma_up(group, date, SMA_20d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_SMA10d_TRENDUP:
		stock_price_check_sma_trendup(group, date, SMA_10d, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_DB:
		stock_price_check_doublebottom(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_MFI_DB:
		stock_price_check_mfi_doublebottom(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_PULLBACK_DB:
		stock_price_check_pullback_doublebottom(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_52W_DB:
		stock_price_check_52w_doublebottom(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_52W_DBUP:
		stock_price_check_52w_doublebottom_up(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_DBUP:
		stock_price_check_doublebottom_up(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_PULLBACK_DBUP:
		stock_price_check_pullback_doublebottom_up(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_STRONG_DBUP:
		stock_price_check_strong_doublebottom_up(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_PB:
		stock_price_check_pullback(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_BO:
		stock_price_check_breakout(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_2nd_BO:
		stock_price_check_2nd_breakout(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_52W_LOWUP:
                stock_price_check_52w_low_up(group, date, symbols_nr, (const char **)symbols);
                break;

	case ACTION_CHECK_CHANGE:
		stock_price_check_change(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_TREND_BO:
		stock_price_check_trend_breakout(group, date, symbols_nr, (const char **)symbols);
		break;
	case ACTION_CHECK_STRONG_UPTREND:
		stock_price_check_strong_uptrend(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_STRONG_BO:
		stock_price_check_strong_breakout(group, date, symbols_nr, (const char **)symbols);
		break;
	case ACTION_CHECK_STRONG_BODY_BO:
		stock_price_check_strong_body_breakout(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_RESIST_BO:
		stock_price_check_resist_breakout(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_MFI:
		stock_price_check_mfi(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_REVERSE_UP:
		stock_price_check_reverse_up(group, date, symbols_nr, (const char **)symbols);
		break;

	case ACTION_CHECK_HIGHER_LOW:
		stock_price_check_higher_low(group, date, symbols_nr, (const char **)symbols);
		break;
	}

finish:
	for (i = 0; i < symbols_nr; i++) {
		if (symbols[i])
			free(symbols[i]);
	}

	return 0;
}
