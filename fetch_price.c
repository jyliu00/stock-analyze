#include "fetch_price.h"
#include "stock_price.h"

#include "util.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

static int run_wget(char *output_fname, char *url)
{
	char *argv[] = { "wget", "-O", output_fname, url, NULL };
	int  status;
	pid_t wget_pid;
	int i;

	switch ((wget_pid = fork( ))) {
	case 0: /* child */
	{
		int null_fd = open("/dev/null", O_WRONLY);
		if (null_fd >= 0) {
			dup2(null_fd, STDOUT_FILENO);
			dup2(null_fd, STDERR_FILENO);
		}

		if (execv("/usr/bin/wget", argv) < 0) {
			anna_error("execv(wget) failed: %d(%s)\n", errno, strerror(errno));
			exit(-1);
		}
	}
		break;

	case -1: /* error */
		anna_error("fork( ) failed: %d(%s)\n", errno, strerror(errno));
		return -1;

	default: /* parent */
		for (i = 0; i < 30; i++) {
			if (waitpid(wget_pid, &status, 0) == wget_pid)
				break;
			sleep(1);
		}

		if (i >= 30) {
			anna_error("wget timeout, url='%s'\n", url);
			kill(wget_pid, SIGKILL);
			waitpid(wget_pid, &status, 0);
			return -1;
		}
		else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			anna_error("wget failed: WIFEXITED=%d, WEXITSTATUS=%d\n",
				   WIFEXITED(status), WEXITSTATUS(status));
			return -1;
		}
		break;
	}

	return 0;
}

static int do_fetch_price(char *output_fname, const char *symbol, int today_only,
			  int year, int month, int mday)
{
	char url[256];

	if (today_only)
		anna_info("Fetch %s today's price ... ", symbol);
	else
		anna_info("\tFetching %s price since %d-%02d-%02d ... ", symbol, year, month + 1, mday);


	if (today_only) {
		snprintf(url, sizeof(url), "http://finance.yahoo.com/d/quotes.csv?s=%s&f=ohgl1v", symbol);
	}
	else {
		snprintf(url, sizeof(url), "http://ichart.finance.yahoo.com/table.csv?s=%s&a=%d&b=%d&c=%d&g=d&ignore=.csv",
			symbol, month, mday, year);
	}

	if (run_wget(output_fname, url) == 0) {
		anna_info("Done.\n");
	}
	else {
		anna_info("Failed.\n");
		return -1;
	}

	return 0;
}

static int stock_history_max_years(void)
{
	return 2;
}

#if 0
static int get_date(char *date, int *year, int *month, int *mday)
{
	char *token;

	token = strtok(date, "-");
	if (!token) return -1;
	*year = atoi(token);

	token = strtok(NULL, "-");
	if (!token) return -1;
	*month = atoi(token);

	token = strtok(NULL, "-");
	if (!token) return -1;
	*mday = atoi(token);

	return 0;
}

static void update_symbol(const char *symbol)
{
	struct stock_price price_history = { };
	struct date_price *latest;
	int year, month, mday;
	char output_fname[128];
	struct stock_price update_price = { };
	int i;

	if (!db_symbol_exist(symbol)) {
		anna_error("symbol '%s' doesn't exist in db\n", symbol);
		return;
	}

	if (db_get_symbol_price_history(symbol, NULL, 0, &price_history) < 0) {
		anna_error("db_get_symbol_price_history(%s) failed\n", symbol);
		return;
	}

	if (price_history.date_cnt == 0) {
		anna_error("%s's price history is empty\n", symbol);
		return;
	}

	latest = &price_history.dateprice[0];

	if (get_date(latest->date, &year, &month, &mday) < 0) {
		anna_error("%s get_date(%s) failed\n", symbol, latest->date);
		return;
	}

	snprintf(output_fname, sizeof(output_fname), "%s.update_price", symbol);

	if (do_fetch_price(output_fname, symbol, 0, year, month - 1, mday) < 0)
		goto finish;

	if (stock_price_get_from_file(output_fname, 0, &update_price) < 0)
		goto finish;

	if (update_price.date_cnt < 2) /* no new date's price */
		goto finish;

	/* don't insert the last date which is already in db */
	update_price.date_cnt -= 1;
	if (db_insert_symbol_price(symbol, &update_price))
		goto finish;

	for (i = 0; i < update_price.date_cnt; i++)
		db_delete_symbol_price_by_date(symbol, price_history.dateprice[price_history.date_cnt - i - 1].date);

	/* update price with new data */
	stock_price_update(symbol);

finish:
	unlink(output_fname);
}
#endif

int fetch_realtime_price(const char *symbol, struct date_price *realtime_price)
{
	char output_fname[128];
	struct date_price price;

	snprintf(output_fname, sizeof(output_fname), ROOT_DIR "/tmp/%s_today.price", symbol);

	if (do_fetch_price(output_fname, symbol, 1, 0, 0, 0) < 0)
		return -1;

	if (stock_price_realtime_from_file(output_fname, &price) < 0)
		return -1;

	memcpy(realtime_price, &price, sizeof(*realtime_price));

	time_t now_t = time(NULL);
	struct tm *now_tm = localtime(&now_t);
	int year = 1900 + now_tm->tm_year;

	snprintf(realtime_price->date, sizeof(realtime_price->date), "%d-%02d-%02d", year, now_tm->tm_mon + 1, now_tm->tm_mday);

	unlink(output_fname);

	return 0;
}

static int fetch_symbol_price_since_date(const char *group, const char *sector, const char *symbol, int year, int month, int mday)
{
	char output_fname[128];
	struct stock_price price = { };
	int rt = -1;

	snprintf(output_fname, sizeof(output_fname), ROOT_DIR "/%s/%s.price", group, symbol);
	if (access(output_fname, F_OK) == 0) {
		//anna_info("%s already fetched, skip\n", symbol);
		return -1;
	}

	snprintf(output_fname, sizeof(output_fname), ROOT_DIR_TMP "/%s.price", symbol);

	if (do_fetch_price(output_fname, symbol, 0, year, month, mday) < 0) {
		anna_error("do_fetch_price(%s, %d-%02d-%02d) failed\n", symbol, year, month, mday);
		goto finish;
	}

	if (stock_price_from_file(output_fname, &price) < 0) {
		anna_error("stock_price_from_file(%s) failed\n", output_fname);
		goto finish;
	}

	if (stock_price_to_file(group, sector, symbol, &price) < 0) {
		anna_error("stock_price_to_file('%s') failed\n", group);
		goto finish;
	}

	rt = 0;

finish:
	unlink(output_fname);

	return rt;
}

int fetch_symbols_price(const char *group, const char *fname, int symbols_nr, const char **symbols)
{
	int count = 0;
	int i;

	/* get last 2 year's price */
	time_t now_t = time(NULL);
	struct tm *now_tm = localtime(&now_t);
	int year = 1900 + now_tm->tm_year - stock_history_max_years();

	if (symbols_nr) {
		for (i = 0; i < symbols_nr; i++)
			fetch_symbol_price_since_date(group, NULL, symbols[i], year, now_tm->tm_mon, now_tm->tm_mday);
		return 0;
	}

	if (fname && fname[0]) {
		char symbol[128];
		char sector[48];
		FILE *fp = fopen(fname, "r");
		if (!fp) {
			anna_error("fopen(%s) failed: %d(%s)\n", fname, errno, strerror(errno));
			return 0;
		}

		time_t start_t = time(NULL);

		while (fgets(symbol, sizeof(symbol), fp)) {
			if (symbol[0] == '#' || symbol[0] == '\n')
				continue;

			int len = strlen(symbol);
			if (symbol[len - 1] == '\n')
				symbol[len - 1] = 0;

			if (symbol[0] == '-') {
				if (strncmp(&symbol[1], "include ", strlen("include ")) == 0)
					count += fetch_symbols_price(group, strchr(symbol, ' ') + 1, 0, NULL);
				continue;
			}
			else if (symbol[0] == '%') {
				if (strncmp(&symbol[1], "sector=", strlen("sector=")) == 0)
					strlcpy(sector, strchr(symbol, '=') + 1, sizeof(sector));
				continue;
			}

			if (fetch_symbol_price_since_date(group, sector, symbol, year, now_tm->tm_mon, now_tm->tm_mday) == 0)
				count += 1;
		}

		anna_info("%zu seconds used by fetching total %d of symbols' price from file %s\n", time(NULL) - start_t, count, fname);
	}

	return count + symbols_nr;
}
