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

static void calculate_candle_stats(struct date_price *cur)
{
	/* calculate candle color */
	if (cur->close > cur->open)
		cur->candle_color = CANDLE_COLOR_GREEN;
	else if (cur->close < cur->open)
		cur->candle_color = CANDLE_COLOR_RED;
	else
		cur->candle_color = CANDLE_COLOR_DOJI;

	/* calculate candle trend */
	uint64_t diff_price = 0;
	uint64_t up_price = cur->close - cur->low;
	uint64_t down_price = cur->high - cur->close;
	if (up_price > down_price) {
		cur->candle_trend = CANDLE_TREND_BULL;
		diff_price = up_price - down_price;
	}
	else if (up_price < down_price) {
		cur->candle_trend = CANDLE_TREND_BEAR;
		diff_price = down_price - up_price;
	}

	if ((diff_price * 100 / cur->open) < 1)
		cur->candle_trend = CANDLE_TREND_DOJI;
}

#define get_2ndlow(dateprice) \
	((dateprice)->candle_color == CANDLE_COLOR_GREEN ? (dateprice)->open : (dateprice)->close)

#define get_2ndhigh(dateprice) \
	((dateprice)->candle_color == CANDLE_COLOR_GREEN ? (dateprice)->close : (dateprice)->open)

static const int min_sr_candle_nr = 5;
static const int max_sr_candle_nr = 20;
static const int diff_margin_percent = 4;

static void calculate_one_side_sr(struct date_price *cur, struct date_price *test_price,
				int *is_low_spt, int *stop_low, int *candle_low_nr, uint64_t *max_low_diff,
				int *is_2ndlow_spt, int *stop_2ndlow, int *candle_2ndlow_nr, uint64_t *max_2ndlow_diff,
				int *is_high_rst, int *stop_high, int *candle_high_nr, uint64_t *max_high_diff,
				int *is_2ndhigh_rst, int *stop_2ndhigh, int *candle_2ndhigh_nr, uint64_t *max_2ndhigh_diff)
{
	uint64_t cur_2ndlow = get_2ndlow(cur);
	uint64_t cur_2ndhigh = get_2ndhigh(cur);
	uint64_t tp_2ndlow = get_2ndlow(test_price);
	uint64_t tp_2ndhigh = get_2ndhigh(test_price);

	if (!(*stop_low) && *is_low_spt) {
		if (test_price->low < cur->low) {
			if (*candle_low_nr < min_sr_candle_nr - 1)
				*is_low_spt = 0;
			else
				*stop_low = 1;
		}
		else if (test_price->close > cur->close) {
			if (test_price->high < cur->low) {
				anna_error("is_low_spt algo error: cur_date=%s, test_date=%s\n", cur->date, test_price->date);
				*stop_low = 1;
			}
			else {
				if (test_price->high - cur->low > *max_low_diff)
					*max_low_diff = test_price->high - cur->low;
				(*candle_low_nr) += 1;
			}
		}
	}

	if (!(*stop_2ndlow) && *is_2ndlow_spt) {
		if (tp_2ndlow < cur_2ndlow) {
			if (*candle_2ndlow_nr < min_sr_candle_nr - 1)
				*is_2ndlow_spt = 0;
			else
				*stop_2ndlow = 1;
		}
		else if (test_price->close > cur->close) {
			if (test_price->high < cur_2ndlow) {
				anna_error("is_2ndlow_spt algo error: cur_date=%s, test_date=%s\n", cur->date, test_price->date);
				*stop_2ndlow = 1;
			}
			else {
				if (test_price->high - cur_2ndlow > *max_2ndlow_diff)
					*max_2ndlow_diff = test_price->high - cur_2ndlow;
				(*candle_2ndlow_nr) += 1;
			}
		}
	}

	if (!(*stop_high) && *is_high_rst) {
		if (test_price->high > cur->high) {
			if (*candle_high_nr < min_sr_candle_nr - 1)
				*is_high_rst = 0;
			else
				*stop_high = 1;
		}
		else if (test_price->close < cur->close) {
			if (cur->high < test_price->low) {
				anna_error("is_high_rst algo error: cur_date=%s, test_date=%s\n", cur->date, test_price->date);
				*stop_high = 1;
			}
			if (cur->high - test_price->low > *max_high_diff)
				*max_high_diff = cur->high - test_price->low;
			(*candle_high_nr) += 1;
		}
	}

	if (!(*stop_2ndhigh) && *is_2ndhigh_rst) {
		if (tp_2ndhigh > cur_2ndhigh) {
			if (*candle_2ndhigh_nr < min_sr_candle_nr - 1)
				*is_2ndhigh_rst = 0;
			else
				*stop_2ndhigh = 1;
		}
		else if (test_price->close < cur->close) {
			if (cur_2ndhigh < test_price->low) {
				anna_error("is_2ndhigh_rst algo error: cur_date=%s, test_date=%s\n", cur->date, test_price->date);
				*stop_2ndhigh = 1;
			}
			else {
				if (cur_2ndhigh - test_price->low > *max_2ndhigh_diff)
					*max_2ndhigh_diff = cur_2ndhigh - test_price->low;
				(*candle_2ndhigh_nr) += 1;
			}
		}
	}
}

static void calculate_support_resistance(struct stock_price *price, int cur_idx, struct date_price *cur)
{
	int cnt = price->date_cnt - cur_idx;
	int left_is_low_spt = 1, left_low_stop = 0, left_is_2ndlow_spt = 1, left_2ndlow_stop = 0;
	int left_is_high_rst = 1, left_high_stop = 0, left_is_2ndhigh_rst = 1, left_2ndhigh_stop = 0;
	uint64_t left_max_low_diff = 0, left_max_2ndlow_diff = 0;
	uint64_t left_max_high_diff = 0, left_max_2ndhigh_diff = 0;
	int left_candle_low_nr = 0, left_candle_2ndlow_nr = 0;
	int left_candle_high_nr = 0, left_candle_2ndhigh_nr = 0;
	int right_is_low_spt = 1, right_low_stop = 0, right_is_2ndlow_spt = 1, right_2ndlow_stop = 0;
	int right_is_high_rst = 1, right_high_stop = 0, right_is_2ndhigh_rst = 1, right_2ndhigh_stop = 0;
	uint64_t right_max_low_diff = 0, right_max_2ndlow_diff = 0;
	uint64_t right_max_high_diff = 0, right_max_2ndhigh_diff = 0;
	int right_candle_low_nr = 0, right_candle_2ndlow_nr = 0;
	int right_candle_high_nr = 0, right_candle_2ndhigh_nr = 0;
	int i;

	/* need at least <min_sr_candle_nr - 1> of candles on left and right */
	if (cnt < min_sr_candle_nr || (price->date_cnt - cnt) < min_sr_candle_nr - 1)
		return;

	/* check left side candles */
	for (i = cur_idx + 1; (i - cur_idx) <= max_sr_candle_nr && i < price->date_cnt; i++) {
		struct date_price *left = &price->dateprice[i];

		if (!left_is_low_spt && !left_is_2ndlow_spt
		    && !left_is_high_rst && !left_is_2ndhigh_rst)
			break;

		if (left_low_stop && left_2ndlow_stop
		    && left_high_stop && left_2ndhigh_stop)
			break;

		calculate_one_side_sr(cur, left,
				      &left_is_low_spt, &left_low_stop, &left_candle_low_nr, &left_max_low_diff,
				      &left_is_2ndlow_spt, &left_2ndlow_stop, &left_candle_2ndlow_nr, &left_max_2ndlow_diff,
				      &left_is_high_rst, &left_high_stop, &left_candle_high_nr, &left_max_high_diff,
				      &left_is_2ndhigh_rst, &left_2ndhigh_stop, &left_candle_2ndhigh_nr, &left_max_2ndhigh_diff);
	}

	/* check right side candles */
	for (i = cur_idx - 1; (cur_idx - i) <= max_sr_candle_nr && i >= 0; i--) {
		struct date_price *right = &price->dateprice[i];

		if (!right_is_low_spt && !right_is_2ndlow_spt
		    && !right_is_high_rst && !right_is_2ndhigh_rst)
			break;

		if (right_low_stop && right_2ndlow_stop
		    && right_high_stop && right_2ndhigh_stop)
			break;

		calculate_one_side_sr(cur, right,
				      &right_is_low_spt, &right_low_stop, &right_candle_low_nr, &right_max_low_diff,
				      &right_is_2ndlow_spt, &right_2ndlow_stop, &right_candle_2ndlow_nr, &right_max_2ndlow_diff,
				      &right_is_high_rst, &right_high_stop, &right_candle_high_nr, &right_max_high_diff,
				      &right_is_2ndhigh_rst, &right_2ndhigh_stop, &right_candle_2ndhigh_nr, &right_max_2ndhigh_diff);
	}

	int left_ok = left_is_low_spt && (left_low_stop || left_candle_low_nr >= min_sr_candle_nr - 1);
	int left_height_ok = (left_max_low_diff * 100 / get_2ndlow(cur) >= diff_margin_percent);
	int right_ok = right_is_low_spt && (right_low_stop || right_candle_low_nr >= min_sr_candle_nr - 1);
	int right_height_ok = (right_max_low_diff * 100 / get_2ndlow(cur) >= diff_margin_percent);

	if (left_ok && right_ok && (left_height_ok || right_height_ok)) {
		cur->sr_flag |= SR_F_SUPPORT_LOW;
		cur->height_low_spt = left_max_low_diff > right_max_low_diff ? left_max_low_diff : right_max_low_diff;
	}

	left_ok = left_is_2ndlow_spt && (left_2ndlow_stop || left_candle_2ndlow_nr >= min_sr_candle_nr - 1);
	left_height_ok = (left_max_2ndlow_diff * 100 / get_2ndlow(cur) >= diff_margin_percent);
	right_ok = right_is_2ndlow_spt && (right_2ndlow_stop || right_candle_2ndlow_nr >= min_sr_candle_nr - 1);
	right_height_ok = (right_max_2ndlow_diff * 100 / get_2ndlow(cur) >= diff_margin_percent);

	if (left_ok && right_ok && (left_height_ok || right_height_ok)) {
		cur->sr_flag |= SR_F_SUPPORT_2ndLOW;
		cur->height_2ndlow_spt = left_max_2ndlow_diff > right_max_2ndlow_diff ? left_max_2ndlow_diff : right_max_2ndlow_diff;
	}

	left_ok = left_is_high_rst && (left_high_stop || left_candle_high_nr >= min_sr_candle_nr - 1);
	left_height_ok = (left_max_high_diff * 100 / get_2ndhigh(cur) >= diff_margin_percent);
	right_ok = right_is_high_rst && (right_high_stop || right_candle_high_nr >= min_sr_candle_nr - 1);
        right_height_ok = (right_max_high_diff * 100 / get_2ndhigh(cur) >= diff_margin_percent);

	if (left_ok && right_ok && (left_height_ok || right_height_ok)) {
		cur->sr_flag |= SR_F_RESIST_HIGH;
		cur->height_high_rst = left_max_high_diff > right_max_high_diff ? left_max_high_diff : right_max_high_diff;
	}

	left_ok = left_is_2ndhigh_rst && (left_2ndhigh_stop || left_candle_2ndhigh_nr >= min_sr_candle_nr - 1);
        left_height_ok = (left_max_2ndhigh_diff * 100 / get_2ndhigh(cur) >= diff_margin_percent);
	right_ok = right_is_2ndhigh_rst && (right_2ndhigh_stop || right_candle_2ndhigh_nr >= min_sr_candle_nr - 1);
	right_height_ok = (right_max_2ndhigh_diff * 100 / get_2ndhigh(cur) >= diff_margin_percent);

	if (left_ok && right_ok && (left_height_ok || right_height_ok)) {
		cur->sr_flag |= SR_F_RESIST_2ndHIGH;
		cur->height_2ndhigh_rst = left_max_2ndhigh_diff > right_max_2ndhigh_diff ? left_max_2ndhigh_diff : right_max_2ndhigh_diff;
	}
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

		calculate_candle_stats(cur);
	}

	for (i = price->date_cnt - 1; i >= 0; i--) {
		struct date_price *cur = &price->dateprice[i];

		calculate_support_resistance(price, i, cur);
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
		if (price->date_cnt >= DATE_PRICE_SZ_MAX) {
			anna_error("fname='%s', date_cnt=%d>%d\n", fname, price->date_cnt, DATE_PRICE_SZ_MAX);
			return -1;
		}

		struct date_price *cur = &price->dateprice[price->date_cnt];
		char *token;
		uint64_t  adj_close;

		token = strtok(buf, ",");
		if (!token) continue;
		strlcpy(cur->date, token, sizeof(cur->date));

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &cur->open);

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &cur->high);

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &cur->low);

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &cur->close);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->volume = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		parse_price(token, &adj_close);

		if (cur->close != adj_close) {
			cur->open = cur->open * adj_close / cur->close;
			cur->high = cur->high * adj_close /cur->close;
			cur->low = cur->low * adj_close / cur->close;
			cur->close = adj_close;
		}

		price->date_cnt += 1;
	}

	fclose(fp);

	calculate_stock_price_statistics(price);

	return 0;
}
