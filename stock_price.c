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

static int sma_days[SMA_NR] = { 10, 20, 30, 50, 60, 100, 120, 200 };
static int vma_days[VMA_NR] = { 10, 20, 60 };

static void calculate_moving_avg(int is_price, struct stock_price *price, int cur_idx, int ma_days, uint64_t *sum, uint64_t *ma)
{
	int cnt = price->date_cnt - cur_idx;
	struct date_price *cur = &price->dateprice[cur_idx];

	(*sum) += is_price ? cur->close : cur->volume;

	if (cnt >= ma_days) {
		if (cnt > ma_days)
			(*sum) -= is_price ? (cur + ma_days)->close : (cur + ma_days)->volume;
		*ma = (*sum) / ma_days;
	}
}

static void calculate_sma(struct stock_price *price, int cur_idx,
			  int sma_type, uint64_t *price_sum, struct date_price *cur)
{
	if (sma_type < 0 || sma_type >= SMA_NR) {
		anna_error("invalid sma_type=%d\n", sma_type);
		return;
	}

	calculate_moving_avg(1, price, cur_idx, sma_days[sma_type], price_sum, &cur->sma[sma_type]);
}

static void calculate_vma(struct stock_price *price, int cur_idx,
			  int vma_type, uint64_t *volume_sum, struct date_price *cur)
{
	if (vma_type < 0 || vma_type >= VMA_NR) {
		anna_error("invalid vma_type=%d\n", vma_type);
		return;
	}

	calculate_moving_avg(0, price, cur_idx, vma_days[vma_type], volume_sum, &cur->vma[vma_type]);
}

static void calculate_stock_price_statistics(struct stock_price *price)
{
	uint64_t price_sum[SMA_NR] = { 0 };
	uint64_t volume_sum[VMA_NR] = { 0 };
	int i, j;

	for (i = price->date_cnt - 1; i >= 0; i--) {
		struct date_price *cur = &price->dateprice[i];

		for (j = 0; j < SMA_NR; j++)
			calculate_sma(price, i, j, &price_sum[j], cur);

		for (j = 0; j < VMA_NR; j++)
			calculate_vma(price, i, j, &volume_sum[j], cur);
printf("date=%s, sma_10d=%zu, sma_20d=%zu, sma_30d=%zu, sma_50d=%zu, sma_60d=%zu, sma_100d=%zu, sma_120d=%zu, sma_200d=%zu, vma_10d=%zu, vma_20d=%zu, vma_50d=%zu\n",
	cur->date, cur->sma[SMA_10d], cur->sma[SMA_20d], cur->sma[SMA_30d], cur->sma[SMA_50d], cur->sma[SMA_60d],
	cur->sma[SMA_100d], cur->sma[SMA_120d], cur->sma[SMA_200d], cur->vma[VMA_10d], cur->vma[VMA_20d], cur->vma[VMA_60d]);
	}
}

int get_stock_price_from_file(const char *fname, int today_only, struct stock_price *price)
{
	FILE *fp;
	char buf[1024];

	if (!fname || !fname[0] || !price) {
		anna_error("invalid input parameters\n");
		return -1;
	}

	price->date_cnt = 0;

	fp = fopen(fname, "r");
	if (!fp) {
		anna_error("fopen(%s) failed: %d(%s)\n", fname, errno, strerror(errno));
		return -1;
	}

	if (!today_only) /* skip the 1st line */
		fgets(buf, sizeof(buf), fp);

	while (fgets(buf, sizeof(buf), fp)) {
		struct date_price *cur_price = &price->dateprice[price->date_cnt];
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

		price->date_cnt += 1;
	}

	fclose(fp);

	calculate_stock_price_statistics(price);

	return 0;
}
