#include "fetch_price.h"
#include "stock_price.h"

#include "sqlite_db.h"
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
		waitpid(wget_pid, &status, 0);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			anna_error("wget failed: WIFEXITED=%d, WEXITSTATUS=%d\n",
				WIFEXITED(status), WEXITSTATUS(status));
			return -1;
		}
	}

	return 0;
}

static int insert_symbol_info_into_db(const char *symbol, const char *info_fname)
{
	char buf[1024] = { 0 };
	char name[64], exchange[16], sector[128], industry[128], country[32];
	int  ticker_found = 0;
	char *p, *e;
	char saved_char;
	int rt = -1;

	FILE *fp = fopen(info_fname, "r");
	if (!fp) {
		anna_error("fopen(%s) failed: %d(%s)\n", info_fname, errno, strerror(errno));
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		if ((p = strstr(buf, "id=\"ticker\"")) != NULL) {
			ticker_found = 1;
			break;
		}
	}

	if (!ticker_found) {
		anna_error("ticker %s is not found in file '%s'\n", symbol, info_fname);
		goto finish;
	}

	/* get exchange */
	if (!(p = strstr(p, ">[")) || !(e = strstr(p, "]<"))) {
		anna_error("exchange is not found for %s\n", symbol);
		goto finish;
	}

	saved_char = *e;
	*e = 0;
	strlcpy(exchange, p + 2, sizeof(exchange));
	*e = saved_char;

	/* get name */
	if (fgets(buf, sizeof(buf), fp) == NULL) {
		anna_error("name line is empty for %s\n", symbol);
		goto finish;
	}

	if (!(p = strstr(buf, "<b>")) || !(e = strstr(p, "</b>"))) {
		anna_error("name is not found for %s\n", symbol);
		goto finish;
	}

	saved_char = *e;
	*e = 0;
	strlcpy(name, p + 3, sizeof(name));
	*e = saved_char;

	/* get sector */
	if (fgets(buf, sizeof(buf), fp) == NULL) {
		anna_error("sector line is not empty for %s\n", symbol);
		goto finish;
	}

	if (!(p = strstr(buf, "f=sec_")) || !(p = strchr(p, '>')) || !(e = strchr(p, '<'))) {
		anna_error("sector is not found for %s\n", symbol);
		goto finish;
	}

	saved_char = *e;
	*e = 0;
	strlcpy(sector, p + 1, sizeof(sector));
	*e = saved_char;

	/* get industry */
	if (!(p = strstr(p, "f=ind_")) || !(p = strchr(p, '>')) || !(e = strchr(p, '<'))) {
		anna_error("industry is not found for %s\n", symbol);
		goto finish;
	}

	saved_char = *e;
	*e = 0;
	strlcpy(industry, p + 1, sizeof(industry));
	*e = saved_char;

	/* get country */
	if (!(p = strstr(p, "f=geo_")) || !(p = strchr(p, '>')) || !(e = strchr(p, '<'))) {
		anna_error("country is not found for %s\n", symbol);
		goto finish;
	}

	saved_char = *e;
	*e = 0;
	strlcpy(country, p + 1, sizeof(country));
	*e = saved_char;

	if (db_insert_stock_info(symbol, name, exchange, sector, industry, country) < 0)
		return -1;

	rt = 0;

finish:
	fclose(fp);

	return rt;
}

static int fetch_symbol_info(const char *symbol)
{
	char output_fname[64];
	char url[256];
	int rt = -1;

	anna_info("\tFetching %s info ... ", symbol);

	snprintf(output_fname, sizeof(output_fname), "%s.info", symbol);
	snprintf(url, sizeof(url), "http://finviz.com/quote.ashx?t=%s", symbol);

	if (run_wget(output_fname, url) < 0)
		goto finish;

	if (insert_symbol_info_into_db(symbol, output_fname) < 0)
		goto finish;

	rt = 0;

finish:
	anna_info("%s.\n", rt == 0 ? "Done" : "Failed");

	unlink(output_fname);

	return rt;
}

static int do_fetch_price(char *output_fname, const char *symbol, int today_only,
			  int year, int month, int mday)
{
	char url[256];

	if (today_only)
		anna_info("\tFetch %s today's price ... ", symbol);
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

static int fetch_symbol_price_since_date(const char *symbol, int year, int month, int mday)
{
	char output_fname[128];
	int today_only = !year;
	struct stock_price price = { };
	time_t t1, t2;

	snprintf(output_fname, sizeof(output_fname), "%s.price", symbol);

	if (do_fetch_price(output_fname, symbol, today_only, year, month, mday) < 0)
		return -1;

	if (get_stock_price_from_file(output_fname, today_only, &price) < 0)
		return -1;

	unlink(output_fname);

	anna_info("\tInsert %s price into db ... ", symbol);

	t1 = time(NULL);

	int rt = db_insert_stock_price(symbol, &price);

	t2 = time(NULL);

	anna_info(" %s (%ld seconds).\n", rt < 0 ? "Failed" : "Done", t2 - t1);

	return rt;
}

static void add_symbol(const char *symbol)
{
	if (fetch_symbol_info(symbol) < 0)
		return;

	/* get last 3 year's price */
	time_t now_t = time(NULL);
	struct tm *now_tm = localtime(&now_t);

	if (fetch_symbol_price_since_date(symbol, 1900 + now_tm->tm_year - 3, now_tm->tm_mon, now_tm->tm_mday) < 0)
		return;
}

static void update_symbol(const char *symbol)
{
}

static int normalize_symbol(char *symbol, const char *orig_symbol, int symbol_sz)
{
	int i;

	strlcpy(symbol, orig_symbol, symbol_sz);

	for (i = 0; symbol[i]; i++) {
		if (symbol[i] >= 'a' && symbol[i] <= 'z') {
			symbol[i] -= 'a' - 'A';
		}
	}

	return 0;
}

static int validate_fection_action_n_symbol(int fetch_action, const char *symbol)
{
	int symbol_exist = db_symbol_exist(symbol);

	switch (fetch_action) {
	case FETCH_ACTION_ADD:
		if (symbol_exist) {
			anna_error("can't add symbol %s, which already exists in db\n", symbol);
			return -1;
		}
		break;

	case FETCH_ACTION_DEL:
		if (!symbol_exist) {
			anna_error("can't delete symbol %s, which doesn't exist in db\n", symbol);
			return -1;
		}
		break;

	case FETCH_ACTION_UPDATE:
		if (!symbol_exist) {
			anna_error("can't update symbol %s, which doesn't exist in db\n", symbol);
			return -1;
		}
		break;

	default:
		anna_error("invalid fetch_action=%d\n", fetch_action);
		return -1;
	}

	return 0;
}

static int fetch_symbol_price(int fetch_action, const char *orig_symbol)
{
	char symbol[32] = { 0 };

	if (normalize_symbol(symbol, orig_symbol, sizeof(symbol)) < 0)
		return -1;

	if (validate_fection_action_n_symbol(fetch_action, symbol) < 0)
		return -1;

	switch (fetch_action) {
	case FETCH_ACTION_ADD:
		anna_info("[Adding symbol %s]\n", symbol);

		add_symbol(symbol);

		anna_info("\n");

		break;

	case FETCH_ACTION_DEL:
		anna_info("[Deleting symbol %s]\n", symbol);

		db_delete_symbol(symbol);

		anna_info("\n");

		break;

	case FETCH_ACTION_UPDATE:
		anna_info("[Updating symbol %s]\n", symbol);

		update_symbol(symbol);

		anna_info("\n");

		break;
	}

	return 0;
}

int fetch_price(int fetch_action, int symbol_nr, const char **symbols)
{
	int i;

	if (symbol_nr) {
		for (i = 0; i < symbol_nr; i++) {
			fetch_symbol_price(fetch_action, symbols[i]);
		}
	}
	else if (fetch_action == FETCH_ACTION_UPDATE) {
	}
	else {
		anna_error("invalid parameters: fetch_action=%d, symbol_nr=%d\n", fetch_action, symbol_nr);
	}

	return 0;
}

int fetch_today_price(const char *symbol, struct date_price *today_price)
{
	char output_fname[128];
	struct stock_price price;

	snprintf(output_fname, sizeof(output_fname), "%s_today.price", symbol);

	if (do_fetch_price(output_fname, symbol, 1, 0, 0, 0) < 0)
		return -1;

	if (get_stock_price_from_file(output_fname, 1, &price) < 0)
		return -1;

	memcpy(today_price, &price.dateprice[0], sizeof(*today_price));

	time_t now_t = time(NULL);
	struct tm *now_tm = localtime(&now_t);
	snprintf(today_price->date, sizeof(today_price->date), "%d-%02d-%02d",
		1900 + now_tm->tm_year, now_tm->tm_mon + 1, now_tm->tm_mday);

	unlink(output_fname);

	return 0;
}
