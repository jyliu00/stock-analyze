#include "fetch_price.h"

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

static int run_wget(char **argv)
{
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
	char name[64], exchange[16];
	int i, j;

	FILE *fp = fopen(info_fname, "r");
	if (!fp) {
		anna_error("fopen(%s) failed: %d(%s)\n", info_fname, errno, strerror(errno));
		return -1;
	}

	fgets(buf, sizeof(buf), fp);

	fclose(fp);

	for (i = 0, j = 1; buf[j]; i++, j++) {
		if (buf[j] == '"')
			break;
		name[i] = buf[j];
	}
	name[i] = 0;

	for (i = 0, j += 3; buf[j]; i++, j++) {
		if (buf[j] == '"')
			break;
		exchange[i] = buf[j];
	}
	exchange[i] = 0;

	if (db_insert_stock_info(symbol, name, exchange) < 0)
		return -1;

	return 0;
}

static int fetch_symbol_info(const char *symbol)
{
	char output_fname[64];
	char url[256];
	char *argv[] = { "wget", "-O", output_fname, url, NULL };
	int rt = -1;

	snprintf(output_fname, sizeof(output_fname), "%s.info", symbol);
	snprintf(url, sizeof(url), "http://finance.yahoo.com/d/quotes.csv?s=%s&f=nx", symbol);

	if (run_wget(argv) < 0)
		return -1;

	if (insert_symbol_info_into_db(symbol, output_fname) < 0)
		goto finish;

	rt = 0;

finish:
	unlink(output_fname);

	return rt;
}

static void add_symbol(const char *symbol)
{
	if (fetch_symbol_info(symbol) < 0)
		return;
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
		add_symbol(symbol);
		break;

	case FETCH_ACTION_DEL:
		break;

	case FETCH_ACTION_UPDATE:
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
