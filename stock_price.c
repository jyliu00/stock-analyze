#include "stock_price.h"

#include "util.h"
#include "fetch_price.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>

static void parse_price(char *price_str, uint32_t *price)
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

static void calculate_moving_avg(int is_price, struct stock_price *price, int cur_idx, int ma_days, uint64_t *sum, uint32_t *ma)
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
		else if (test_price->high < cur->low) {
			anna_error("is_low_spt algo error: cur_date=%s, test_date=%s\n", cur->date, test_price->date);
			(*is_low_spt) = 0;
		}
		else {
			if (test_price->high - cur->low > *max_low_diff)
				*max_low_diff = test_price->high - cur->low;
			(*candle_low_nr) += 1;
		}
	}

	if (!(*stop_2ndlow) && *is_2ndlow_spt) {
		if (tp_2ndlow < cur_2ndlow) {
			if (*candle_2ndlow_nr < min_sr_candle_nr - 1)
				*is_2ndlow_spt = 0;
			else
				*stop_2ndlow = 1;
		}
		else if (test_price->high < cur_2ndlow) {
			anna_error("is_2ndlow_spt algo error: cur_date=%s, test_date=%s\n", cur->date, test_price->date);
			(*is_2ndlow_spt) = 0;
		}
		else {
			if (test_price->high - cur_2ndlow > *max_2ndlow_diff)
				*max_2ndlow_diff = test_price->high - cur_2ndlow;
			(*candle_2ndlow_nr) += 1;
		}
	}

	if (!(*stop_high) && *is_high_rst) {
		if (test_price->high > cur->high) {
			if (*candle_high_nr < min_sr_candle_nr - 1)
				*is_high_rst = 0;
			else
				*stop_high = 1;
		}
		else if (cur->high < test_price->low) {
			anna_error("is_high_rst algo error: cur_date=%s, test_date=%s\n", cur->date, test_price->date);
			(*is_high_rst) = 0;
		}
		else {
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
		else if (cur_2ndhigh < test_price->low) {
			anna_error("is_2ndhigh_rst algo error: cur_date=%s, test_date=%s\n", cur->date, test_price->date);
			*(is_2ndhigh_rst) = 0;
		}
		else {
			if (cur_2ndhigh - test_price->low > *max_2ndhigh_diff)
				*max_2ndhigh_diff = cur_2ndhigh - test_price->low;
			(*candle_2ndhigh_nr) += 1;
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
#if 0
	if ((cur->sr_flag & (SR_F_SUPPORT_LOW | SR_F_SUPPORT_2ndLOW))
	    && (cur->sr_flag & (SR_F_RESIST_HIGH | SR_F_RESIST_2ndHIGH)))
	{
		anna_error("date=%s is set to both support and resistance, sr_flag=0x%02x\n", cur->date, cur->sr_flag);
		cur->sr_flag = 0;
	}
#endif
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

#if 0
static void update_moving_average(struct stock_price *price_history)
{
	int i, j;

	for (i = 0; i < price_history->date_cnt; i++) {
		struct date_price *cur = &price_history->dateprice[i];
		uint64_t sma[SMA_NR] = { 0 }, vma[SMA_NR] = { 0 };
		int cnt;

		if (cur->sma[SMA_30d]) /* already updated */
			break;

		for (j = i, cnt = j - i; cnt < 200 && j < price_history->date_cnt; j++, cnt = j - i) {
			struct date_price *dp = &price_history->dateprice[j];

			if (cnt < 10) {
				sma[SMA_10d] += dp->close;
				vma[VMA_10d] += dp->volume;
			}

			if (cnt < 20) {
				sma[SMA_20d] += dp->close;
				vma[VMA_20d] += dp->volume;
			}

			if (cnt < 30)
				sma[SMA_30d] += dp->close;

			if (cnt < 50)
				sma[SMA_50d] += dp->close;

			if (cnt < 60) {
				sma[SMA_60d] += dp->close;
				vma[VMA_60d] += dp->volume;
			}

			if (cnt < 100)
				sma[SMA_100d] += dp->close;

			if (cnt < 120)
				sma[SMA_120d] += dp->close;

			if (cnt < 200)
				sma[SMA_200d] += dp->close;
		}

		if (cnt >= 9) {
			cur->sma[SMA_10d] = sma[SMA_10d] / sma_days[SMA_10d];
			cur->vma[VMA_10d] = vma[VMA_10d] / vma_days[VMA_10d];
		}

		if (cnt >= 19) {
			cur->sma[SMA_20d] = sma[SMA_20d] / sma_days[SMA_20d];
			cur->vma[VMA_20d] = vma[VMA_20d] / vma_days[VMA_20d];
		}

		if (cnt >= 29)
			cur->sma[SMA_30d] = sma[SMA_30d] / sma_days[SMA_30d];

		if (cnt >= 49)
			cur->sma[SMA_50d] = sma[SMA_50d] / sma_days[SMA_50d];

		if (cnt >= 59) {
			cur->sma[SMA_60d] = sma[SMA_60d] / sma_days[SMA_60d];
			cur->vma[VMA_60d] = vma[VMA_60d] / vma_days[VMA_60d];
		}

		if (cnt >= 99)
			cur->sma[SMA_100d] = sma[SMA_100d] / sma_days[SMA_100d];

		if (cnt >= 119)
			cur->sma[SMA_120d] = sma[SMA_120d] / sma_days[SMA_120d];

		if (cnt >= 199)
			cur->sma[SMA_200d] = sma[SMA_200d] / sma_days[SMA_200d];

		calculate_candle_stats(cur);
	}
}

static void update_support_resistance(struct stock_price *price_history)
{
	int new_date_cnt = 0;
	int i;

	/* update support/resistance */
	for (i = 0; i < price_history->date_cnt; i++) {
		struct date_price *cur = &price_history->dateprice[i];

		if (cur->updated)
			new_date_cnt += 1;
		else
			break;
	}

	if (new_date_cnt == 0)
		return;

	for (i = max_sr_candle_nr - 2 + new_date_cnt; i >= 0; i--) {
		struct date_price *cur = &price_history->dateprice[i];
		uint16_t  sr_flag = cur->sr_flag;
		uint64_t  height_low_spt = cur->height_low_spt;
		uint64_t  height_2ndlow_spt = cur->height_2ndlow_spt;
		uint64_t  height_high_rst = cur->height_high_rst;
		uint64_t  height_2ndhigh_rst = cur->height_2ndhigh_rst;

		calculate_support_resistance(price_history, i, cur);

		if (cur->sr_flag != sr_flag
		    || cur->height_low_spt != height_low_spt
		    || cur->height_2ndlow_spt != height_2ndlow_spt
		    || cur->height_high_rst != height_high_rst
		    || cur->height_2ndhigh_rst != height_2ndhigh_rst)
			cur->updated = 1;
	}
}
#endif

void stock_price_update(const char *symbol)
{
#if 0
	struct stock_price price_history = { };

	if (db_get_symbol_price_history(symbol, NULL, 0, &price_history) < 0)
		return;

	/* update sma/vma */
	update_moving_average(&price_history);

	/* update support/resistance */
	update_support_resistance(&price_history);

	/* update price in db */
	db_update_symbol_price(symbol, &price_history);
#endif
}

int stock_price_history_from_file(const char *fname, struct stock_price *price)
{
	FILE *fp;
	char buf[1024];
	int i;

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

	while (fgets(buf, sizeof(buf), fp)) {
		if (price->date_cnt >= DATE_PRICE_SZ_MAX) {
			anna_error("fname='%s', date_cnt=%d>%d\n", fname, price->date_cnt, DATE_PRICE_SZ_MAX);
			return -1;
		}

		struct date_price *cur = &price->dateprice[price->date_cnt];
		char *token;

		token = strtok(buf, ",");
		if (!token) continue;
		strlcpy(cur->date, token, sizeof(cur->date));

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->open = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->high = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->low = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->close = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->volume = atoi(token);


		for (i = 0; i < SMA_NR; i++) {
			token = strtok(NULL, ",");
			if (!token) continue;
			cur->sma[i] = atoi(token);
		}

		for (i = 0; i < VMA_NR; i++) {
			token = strtok(NULL, ",");
			if (!token) continue;
			cur->vma[i] = atoi(token);
		}

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->candle_color = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->candle_trend = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->sr_flag = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->height_low_spt = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->height_2ndlow_spt = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->height_high_rst = atoi(token);

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->height_2ndhigh_rst = atoi(token);

		price->date_cnt += 1;
	}

	fclose(fp);

	return 0;
}

int stock_price_realtime_from_file(const char *output_fname, struct date_price *price)
{
	char buf[1024];
	char *token;

	FILE *fp = fopen(output_fname, "r");
	if (!fp) {
		anna_error("fopen(%s) failed: %d(%s)\n", output_fname, errno, strerror(errno));
		return -1;
	}

	if (!fgets(buf, sizeof(buf), fp)) {
		anna_error("fgets(%s) failed: %d(%s)\n", output_fname, errno, strerror(errno));
		return -1;
	}

	token = strtok(buf, ",");
	if (!token) return -1;
	parse_price(token, &price->open);

	token = strtok(NULL, ",");
	if (!token) return -1;
	parse_price(token, &price->high);

	token = strtok(NULL, ",");
	if (!token) return -1;
	parse_price(token, &price->low);

	token = strtok(NULL, ",");
	if (!token) return -1;
	parse_price(token, &price->close);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->volume = atoi(token);

	fclose(fp);

	return 0;
}

int stock_price_from_file(const char *fname, struct stock_price *price)
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

	/* skip the 1st line */
	fgets(buf, sizeof(buf), fp);

	while (fgets(buf, sizeof(buf), fp)) {
		if (price->date_cnt >= DATE_PRICE_SZ_MAX) {
			anna_error("fname='%s', date_cnt=%d>%d\n", fname, price->date_cnt, DATE_PRICE_SZ_MAX);
			return -1;
		}

		struct date_price *cur = &price->dateprice[price->date_cnt];
		char *token;
		uint32_t  adj_close;

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
			uint32_t diff = cur->close > adj_close ? cur->close - adj_close : adj_close - cur->close;

			if (diff * 100 / cur->close > 5) {
				cur->open = ((uint64_t)cur->open) * adj_close / cur->close;
				cur->high = ((uint64_t)cur->high) * adj_close /cur->close;
				cur->low = ((uint64_t)cur->low) * adj_close / cur->close;
				cur->close = adj_close;
			}
		}

		price->date_cnt += 1;
	}

	fclose(fp);

	if (price->date_cnt > 20)
		calculate_stock_price_statistics(price);

	return 0;
}

int stock_price_to_file(const char *country, const char *symbol, const struct stock_price *price)
{
	char output_fname[256];
	FILE *fp;
	int i;

	snprintf(output_fname, sizeof(output_fname), ROOT_DIR "/%s/%s.price", country, symbol);
	fp = fopen(output_fname, "w");
	if (!fp) {
		anna_error("fopen(%s) failed: %d(%s)\n", output_fname, errno, strerror(errno));
		return -1;
	}

	for (i = 0; i < price->date_cnt; i++) {
		const struct date_price *p = &price->dateprice[i];

		fprintf(fp,
			"%s,%u,%u,%u,%u,%u," /* date, open, high, low, close, volume */
			"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u," /* sma_10/20/30/50/60/100/120/200d, vma_10/20/60d */
			"%u,%u,%u,%u,%u,%u,%u\n",
			p->date, p->open, p->high, p->low, p->close, p->volume,
			p->sma[SMA_10d], p->sma[SMA_20d], p->sma[SMA_30d], p->sma[SMA_50d],
			p->sma[SMA_60d], p->sma[SMA_100d], p->sma[SMA_120d], p->sma[SMA_200d],
			p->vma[VMA_10d], p->vma[VMA_20d], p->vma[VMA_60d],
			p->candle_color, p->candle_trend, p->sr_flag,
			p->height_low_spt, p->height_2ndlow_spt, p->height_high_rst, p->height_2ndhigh_rst);
	}

	fclose(fp);

	return 0;
}


struct stock_support
{
#define STOCK_SUPPORT_MAX_DATES  48
	char date[STOCK_SUPPORT_MAX_DATES][STOCK_DATE_SZ];
	uint8_t sr_flag[STOCK_SUPPORT_MAX_DATES];
	int date_nr;
};

static int sr_height_margin_datecnt(uint64_t height, uint64_t base, int datecnt)
{
	if (datecnt <= 63) { /* 0 ~ 3 month */
		return (height * 100 / base >= 4) ? 1 : 0;
	}
	else if (datecnt <= 126) { /* 3 ~ 6 month */
		return (height * 100 /base >= 5) ? 1 : 0;
	}
	else if (datecnt <= 252) { /* 6 month ~ 1 year */
		return (height * 100 /base >= 6) ? 1 : 0;
	}
	else { /* > 1 year */
		return (height * 100 / base >= 8) ? 1 : 0;
	}
}

static int sr_hit(uint64_t price2check, uint64_t base_price)
{
	uint64_t diff = price2check > base_price ? price2check - base_price : base_price - price2check;
	return (diff * 1000 / base_price <= 10);
}

static void date2sspt_copy(const struct date_price *prev, struct stock_support *sspt)
{
	strlcpy(sspt->date[sspt->date_nr], prev->date, sizeof(sspt->date[0]));
	sspt->sr_flag[sspt->date_nr] = prev->sr_flag;
	sspt->date_nr += 1;
}

static void check_support(const struct stock_price *price_history, const struct date_price *price2check, struct stock_support *sspt)
{
	int i;

	sspt->date_nr = 0;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		int datecnt = i + 1;
		uint64_t prev_2ndhigh = get_2ndhigh(prev);
		uint64_t prev_2ndlow = get_2ndlow(prev);
		uint64_t price2check_2ndlow = get_2ndlow(price2check);

		if ((prev->sr_flag & SR_F_SUPPORT_LOW)
		    && sr_height_margin_datecnt(prev->height_low_spt, prev_2ndlow, datecnt))
		{
			if (sr_hit(price2check->low, prev->low) || sr_hit(price2check_2ndlow, prev->low)) {
				date2sspt_copy(prev, sspt);
				continue;
			}
		}

		if ((prev->sr_flag & SR_F_SUPPORT_2ndLOW)
		    && sr_height_margin_datecnt(prev->height_2ndlow_spt, prev_2ndlow, datecnt))
		{
			if (sr_hit(price2check->low, prev_2ndlow) || sr_hit(price2check_2ndlow, prev_2ndlow)) {
				date2sspt_copy(prev, sspt);
				continue;
			}
		}

		if ((prev->sr_flag & SR_F_RESIST_HIGH)
		    && sr_height_margin_datecnt(prev->height_high_rst, prev_2ndhigh, datecnt))
		{
			if (sr_hit(price2check->low, prev->high) || sr_hit(price2check_2ndlow, prev->high)) {
				date2sspt_copy(prev, sspt);
				continue;
			}
		}

		if ((prev->sr_flag & SR_F_RESIST_2ndHIGH)
		    && sr_height_margin_datecnt(prev->height_2ndhigh_rst, prev_2ndhigh, datecnt))
		{
			if (sr_hit(price2check->low, prev_2ndhigh) || sr_hit(price2check_2ndlow, prev_2ndhigh)) {
				date2sspt_copy(prev, sspt);
				continue;
			}
		}
	}
}

static int get_stock_price2check(const char *symbol, const char *date,
				const struct stock_price *price_history,
				struct date_price *price2check)
{
	if (date && date[0]) {
		int i;
		for (i = 0; i < price_history->date_cnt; i++) {
			const struct date_price *cur = &price_history->dateprice[i];
			if (!strcmp(cur->date, date)) {
				memcpy(price2check, cur, sizeof(*price2check));
				break;
			}
		}

		if (i == price_history->date_cnt) {
			anna_error("date='%s' is  not found in history price\n", date);
			return -1;
		}
	}
	else { /* get real time price */
		if (fetch_realtime_price(symbol, price2check) < 0)
			return -1;
	}

	return 0;
}

static void symbol_check_support(const char *symbol, const char *fname, const char *date)
{
	struct stock_price price_history;
	struct date_price price2check;
	struct stock_support sspt;
	int i;

	if (stock_price_history_from_file(fname, &price_history) < 0) {
		anna_error("stock_price_history_from_file(%s) failed\n", fname);
		return;
	}

	if (get_stock_price2check(symbol, date, &price_history, &price2check) < 0) {
		anna_error("%s: get_stock_price2check(%s)\n", symbol, date);
		return;
	}

	check_support(&price_history, &price2check, &sspt);

	if (!sspt.date_nr) {
		anna_info("%s: date=%s, is NOT at any support dates\n\n", symbol, price2check.date);
		return;
	}

	anna_info("\n%s: date=%s is supported by %d dates:", symbol, date, sspt.date_nr);

	for (i = 0; i < sspt.date_nr; i++)
		anna_info(" %s(%c)", sspt.date[i], is_support(sspt.sr_flag[i]) ? 's' : is_resist(sspt.sr_flag[i]) ? 'r' : '?');

	anna_info("\n");
}

void stock_price_check_support(const char *country, const char *date, int symbols_nr, const char **symbols)
{
	char path[128];
	char fname[256];
	int i;

	snprintf(path, sizeof(path), "%s/%s", ROOT_DIR, country);

	if (symbols_nr) {
		for (i = 0; i < symbols_nr; i++) {
			snprintf(fname, sizeof(fname), "%s/%s.price", path, symbols[i]);

			symbol_check_support(symbols[i], fname, date);
		}
	}
	else {	
		DIR *dir = opendir(path);
		if (!dir) {
			anna_error("opendir(%s) failed: %d(%s)\n", path, errno, strerror(errno));
			return;
		}

		struct dirent *de;

		while ((de = readdir(dir))) {
			char symbol[16];
			char *p;

			if (de->d_name[0] == '.')
				continue;

			strlcpy(symbol, de->d_name, sizeof(symbol));
			p = strstr(symbol, ".price");
			if (p)
				*p = 0;

			snprintf(fname, sizeof(fname), "%s/%s", path, de->d_name);

			symbol_check_support(symbol, fname, date);
		}

		closedir(dir);
	}
}
