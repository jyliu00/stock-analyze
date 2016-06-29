#include "stock_price.h"

#include "util.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static void parse_price(char *price_str, uint64_t *price)
{
	char *dot = strchr(price_str, '.');

	if (!dot) {
		*price = atoi(price_str) * 1000;
	}
	else {
		int i;
		char saved_char = *dot;
		*dot = 0;

		*price = atoi(price_str) * 1000;

		*dot = saved_char;

		for (i = 1; *(dot + i) && isdigit(*(dot + i)) && i <= 3; i++)
			*price += (*(dot + i) - '0') * (i == 1 ? 100 : ((i == 2) ? 10 : 1));

		if (i == 4 && *(dot + i) && isdigit(*(dot + i)) && *(dot + i) >= '5')
			*price += 1;
	}
}

static void calculate_stock_price_average(int price_cnt, struct stock_price *prices)
{
	int i;

	for (i = 0; i < price_cnt; i++) {
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
		char *token;
		uint64_t  adj_close;

		token = strtok(buf, ",");
		if (!token) continue;
		strlcpy(cur_price->date, token, sizeof(cur_price->date));

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &cur_price->open);

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &cur_price->high);

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &cur_price->low);

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &cur_price->close);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur_price->volume = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &adj_close);

		if (cur_price->close != adj_close) {
			cur_price->open = cur_price->open * adj_close / cur_price->close;
			cur_price->high = cur_price->high * adj_close /cur_price->close;
			cur_price->low = cur_price->low * adj_close / cur_price->close;
			cur_price->close = adj_close;
		}

		(*price_cnt) += 1;
	}

	fclose(fp);

	calculate_stock_price_average(*price_cnt, prices);

	return 0;
}
