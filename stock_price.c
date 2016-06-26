#include "stock_price.h"

#include "util.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static void parse_price(char *price_str, int *price)
{
	char *dot = strchr(price_str, '.');

	if (dot) {
		*price = atoi(price_str) * 1000;
	}
	else {
		int i;
		char saved_char = *dot;
		*dot = 0;

		*price = atoi(price_str) * 1000;

		*dot = saved_char;

		for (i = 1; *(dot + i) && isdigit(*(dot+i)) && i <= 3; i++)
			*price += (*(dot + i) - '0') * (i < 3 ? (3 - i) * 10 : 1);
	}
}

int get_stock_price_from_file(const char *fname, int today_only, struct stock_price *prices, int *price_cnt)
{
	FILE *fp;
	char buf[1024];

	if (!fname || !fname[0] || !prices || !price_cnt) {
		anna_error("invalid input parameters\n");
		return -1;
	}

	*price_cnt = 0;

	fp = fopen(fname, "r");
	if (!fp) {
		anna_error("fopen(%s) failed: %d(%s)\n", fname, errno, strerror(errno));
		return -1;
	}

	if (!today_only) /* skip the 1st line */
		fgets(buf, sizeof(buf), fp);

	while (fgets(buf, sizeof(buf), fp)) {
		struct stock_price *cur_price = &prices[*price_cnt];

		char open_str[32], high_str[32], low_str[32],
		     close_str[32], adj_close_str[32];
		int  adj_close;

		sscanf(buf, "%s,%s,%s,%s,%s,%d,%s", cur_price->date, open_str, high_str,
			low_str, close_str, &cur_price->volume, adj_close_str);

		parse_price(open_str, &cur_price->open);
		parse_price(high_str, &cur_price->high);
		parse_price(low_str, &cur_price->low);
		parse_price(close_str, &cur_price->close);
		parse_price(adj_close_str, &adj_close);

		(*price_cnt) += 1;
	}

	fclose(fp);

	return 0;
}
