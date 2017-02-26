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
#include <time.h>

static int sma2check = -1;
static int weeks2check = 0;
static int selected_symbol_nr = 0;

const char *candle_color[CANDLE_COLOR_NR] = { "doji", "green", "red" };
const char *candle_trend[CANDLE_TREND_NR] = { "doji", "bull", "bear" };

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

static void calculate_mfi(struct stock_price *price, int cur_idx, struct date_price *cur,
			  uint64_t *positive_raw_mf, uint64_t *negative_raw_mf)
{
	struct date_price *prev;
	int cnt = price->date_cnt - cur_idx;

	cur->typical_price = (cur->high + cur->low + cur->close) / 3;
	cur->raw_mf = (uint64_t)cur->typical_price * cur->volume;

	/* add current date's raw money flow */
	if (cnt == 1)
		*positive_raw_mf += cur->raw_mf;
	else {
		prev = cur + 1;
		if (cur->typical_price >= prev->typical_price)
			*positive_raw_mf += cur->raw_mf;
		else
			*negative_raw_mf += cur->raw_mf;
	}

	if (cnt < 14)
		return;

	/* delete 15days-ago's money flow */
	if (cnt > 14) {
		prev = cur + 14;
		if (cnt == 15 || prev->typical_price >= (prev+1)->typical_price)
			*positive_raw_mf -= prev->raw_mf;
		else
			*negative_raw_mf -= prev->raw_mf;
	}

	cur->mfi = 10000 - 1000000 / (100 + (*positive_raw_mf * 100 / (*negative_raw_mf ? *negative_raw_mf : 100)));
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

	if (cur->open && (diff_price * 100 / cur->open) < 1) {
		if (cur->high - cur->low == 0)
			cur->candle_trend = CANDLE_TREND_DOJI;
		else if (up_price == 0)
			cur->candle_trend = CANDLE_TREND_BEAR;
		else if (down_price == 0)
			cur->candle_trend = CANDLE_TREND_BULL;
		else if (up_price > down_price && up_price * 100 / (cur->high - cur->low) >= 60)
			cur->candle_trend = CANDLE_TREND_BULL;
		else if (up_price < down_price && down_price * 100 / (cur->high - cur->low) >= 60)
			cur->candle_trend = CANDLE_TREND_BEAR;
	}
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

static void calculate_support_bigupday(struct stock_price *price, int cur_idx, struct date_price *cur)
{
	int i;

	/* 1st candle must be green and have a good body size */
	if (cur->sr_flag
	    || cur->candle_color != CANDLE_COLOR_GREEN
	    || (uint64_t)(cur->close - cur->open) * 1000 / (cur->high - cur->low) < 700) /* body size >= 70% */
		return;

	uint32_t cur_2ndlow = get_2ndlow(cur);
	uint32_t high = cur->high;

	for (i = cur_idx - 1; i >= 0; i--) {
		struct date_price *next = &price->dateprice[i];

		if (next->low < cur_2ndlow || next->close <= (next + 1)->close)
			break;

		if (next->high > high)
			high = next->high;
	}

	if ((high - cur->low) * 1000 / cur->low >= 70) /* up >= 7% */
		cur->sr_flag = SR_F_BIGUPDAY;
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

	if (cur->sr_flag)
		return;

	calculate_support_bigupday(price, cur_idx, cur);
}

static void calculate_stock_price_statistics(struct stock_price *price)
{
	uint64_t price_sum[SMA_NR] = { 0 };
	uint64_t volume_sum[VMA_NR] = { 0 };
	uint64_t positive_raw_mf = 0, negative_raw_mf = 0;
	int i, j;

	for (i = price->date_cnt - 1; i >= 0; i--) {
		struct date_price *cur = &price->dateprice[i];

		for (j = 0; j < SMA_NR; j++)
			calculate_sma(price, i, j, &price_sum[j], cur);

		for (j = 0; j < VMA_NR; j++)
			calculate_vma(price, i, j, &volume_sum[j], cur);

		calculate_mfi(price, i, cur, &positive_raw_mf, &negative_raw_mf);

		calculate_candle_stats(cur);
	}

	for (i = price->date_cnt - 1; i >= 0; i--) {
		struct date_price *cur = &price->dateprice[i];

		if (cur->open && cur->high && cur->low && cur->close)
			calculate_support_resistance(price, i, cur);
	}
}

static int str_to_price(char *buf, struct date_price *price)
{
	char *token;
	int i;

	token = strtok(buf, ",");
	if (!token) return -1;
	strlcpy(price->date, token, sizeof(price->date));

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->wday = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->open = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->high = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->low = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->close = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->volume = atoi(token);


	for (i = 0; i < SMA_NR; i++) {
		token = strtok(NULL, ",");
		if (!token) return -1;
		price->sma[i] = atoi(token);
	}

	for (i = 0; i < VMA_NR; i++) {
		token = strtok(NULL, ",");
		if (!token) return -1;
		price->vma[i] = atoi(token);
	}

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->mfi = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->candle_color = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->candle_trend = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->sr_flag = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->height_low_spt = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->height_2ndlow_spt = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->height_high_rst = atoi(token);

	token = strtok(NULL, ",");
	if (!token) return -1;
	price->height_2ndhigh_rst = atoi(token);

	return 0;
}

int stock_price_history_from_file(const char *fname, struct stock_price *price)
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

	price->sector[0] = 0;

	while (fgets(buf, sizeof(buf), fp)) {
		if (buf[0] == '#')
			continue;

		if (buf[0] == '%') {
			buf[strlen(buf) - 1] = 0;
			if (strncmp(&buf[1], "sector=", strlen("sector=")) == 0)
				strlcpy(price->sector, strchr(buf, '=') + 1, sizeof(price->sector));
			continue;
		}

		if (price->date_cnt >= DATE_PRICE_SZ_MAX) {
			anna_error("fname='%s', date_cnt=%d>%d\n", fname, price->date_cnt, DATE_PRICE_SZ_MAX);
			return -1;
		}

		if (str_to_price(buf, &price->dateprice[price->date_cnt]) < 0)
			continue;

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

	if (!isdigit(buf[0]))
		return -1;

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

	calculate_candle_stats(price);

	return 0;
}

static int month_str2int(const char *str)
{
	if (strncmp(str, "Jan", 3) == 0)
		return 1;
	else if (strncmp(str, "Feb", 3) == 0)
		return 2;
	else if (strncmp(str, "Mar", 3) == 0)
		return 3;
	else if (strncmp(str, "Apr", 3) == 0)
		return 4;
	else if (strncmp(str, "May", 3) == 0)
		return 5;
	else if (strncmp(str, "Jun", 3) == 0)
		return 6;
	else if (strncmp(str, "Jul", 3) == 0)
		return 7;
	else if (strncmp(str, "Aug", 3) == 0)
		return 8;
	else if (strncmp(str, "Sep", 3) == 0)
		return 9;
	else if (strncmp(str, "Oct", 3) == 0)
		return 10;
	else if (strncmp(str, "Nov", 3) == 0)
		return 11;
	else if (strncmp(str, "Dec", 3) == 0)
		return 12;
	return 0;
}

static int str2date(const char *date, int *year, int *month, int *mday)
{
	char _date[32];
	char *token, *saved;

	strlcpy(_date, date, sizeof(_date));

	switch (fetch_source) {
	case FETCH_SOURCE_GOOGLE:
		token = strtok_r(_date, "-", &saved);
		if (!token) return -1;
		*mday = atoi(token);

		token = strtok_r(NULL, "-", &saved);
		if (!token) return -1;
		*month = month_str2int(token);

		token = strtok_r(NULL, "-", &saved);
		if (!token) return -1;
		*year = 2000 + atoi(token);

		break;

	case FETCH_SOURCE_YAHOO:
		token = strtok_r(_date, "-", &saved);
		if (!token) return -1;
		*year = atoi(token);

		token = strtok_r(NULL, "-", &saved);
		if (!token) return -1;
		*month = atoi(token);

		token = strtok_r(NULL, "-", &saved);
		if (!token) return -1;
		*mday = atoi(token);

		break;

	default:
		return -1;
	}

	return 0;
}

static int dayofweek(int year, int month, int mday)
{
	static int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	year -= month < 3;
	return ( year + year/4 - year/100 + year/400 + t[month-1] + mday) % 7;
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
		int year, month, mday;

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

		if (str2date(cur->date, &year, &month, &mday) < 0)
			anna_error("str2date(%s) failed\n", cur->date);
		else {
			snprintf(cur->date, sizeof(cur->date), "%04d-%02d-%02d", year, month, mday);
			cur->wday = dayofweek(year, month, mday);
		}

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

		if (fetch_source == FETCH_SOURCE_GOOGLE)
			goto next_date;

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
next_date:
		price->date_cnt += 1;
	}

	fclose(fp);

	if (price->date_cnt > 20)
		calculate_stock_price_statistics(price);

	return 0;
}

void fprintf_date_price(FILE *fp, const struct date_price *p)
{
	fprintf(fp,
		"%s,%u,%u,%u,%u,%u,%u," /* date, wday, open, high, low, close, volume */
		"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u," /* sma_10/20/30/50/60/100/120/200d, vma_10/20/60d, mfi */
		"%u,%u,%u,%u,%u,%u,%u\n",
		p->date, p->wday, p->open, p->high, p->low, p->close, p->volume,
		p->sma[SMA_10d], p->sma[SMA_20d], p->sma[SMA_30d], p->sma[SMA_50d],
		p->sma[SMA_60d], p->sma[SMA_100d], p->sma[SMA_120d], p->sma[SMA_200d],
		p->vma[VMA_10d], p->vma[VMA_20d], p->vma[VMA_60d], p->mfi,
		p->candle_color, p->candle_trend, p->sr_flag,
		p->height_low_spt, p->height_2ndlow_spt, p->height_high_rst, p->height_2ndhigh_rst);
}

int stock_price_to_file(const char *group, const char *sector, const char *symbol, const struct stock_price *price)
{
	char output_fname[256];
	FILE *fp;
	int i;

	snprintf(output_fname, sizeof(output_fname), ROOT_DIR "/%s/%s.price", group, symbol);
	fp = fopen(output_fname, "w");
	if (!fp) {
		anna_error("fopen(%s) failed: %d(%s)\n", output_fname, errno, strerror(errno));
		return -1;
	}

	if (sector && sector[0])
		fprintf(fp, "%%sector=%s\n", sector);

	fprintf(fp, "# date,wday, open, high, low, close, volume, sma10d,20d,30d,50d,60d,100d,120d,200d, "
		    "vma10d,20d,60d, mfi, candle_color, candle_trend, sr_flag, height_low_spt,2ndlow_spt,high_rst,2ndhigh_rst\n");

	for (i = 0; i < price->date_cnt; i++) {
		fprintf_date_price(fp, &price->dateprice[i]);
	}

	fclose(fp);

	return 0;
}


struct stock_support
{
#define STOCK_SUPPORT_MAX_DATES  48
	char date[STOCK_SUPPORT_MAX_DATES][STOCK_DATE_SZ];
	uint8_t sr_flag[STOCK_SUPPORT_MAX_DATES];
	int8_t  is_doublebottom[STOCK_SUPPORT_MAX_DATES];
	uint32_t  mfi[STOCK_SUPPORT_MAX_DATES];
	int date_nr;
	uint32_t avg_spt_price;
};

static int sr_height_margin_datecnt(uint64_t height, uint64_t base, int datecnt)
{
	return (height * 1000 / base >= sr_height_margin);
#if 0
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
#endif
}

static int sr_hit(uint64_t price2check, uint64_t base_price)
{
	uint64_t diff = price2check > base_price ? price2check - base_price : base_price - price2check;
	return (diff * 1000 / base_price <= 15);
}

static int sma_hit(uint64_t price2check, uint64_t sma_price)
{
	uint64_t diff = price2check > sma_price ? price2check - sma_price : sma_price - price2check;
	return (diff * 1000 / sma_price <= 10);
}

static int bo_hit(uint64_t price2check, uint64_t base_price)
{
	uint64_t diff = price2check > base_price ? price2check - base_price : base_price - price2check;
	return (diff * 1000 / base_price >= 20);
}

static void date2sspt_copy(const struct date_price *prev, struct stock_support *sspt, int8_t is_db)
{
	strlcpy(sspt->date[sspt->date_nr], prev->date, sizeof(sspt->date[0]));
	sspt->sr_flag[sspt->date_nr] = prev->sr_flag;
	sspt->is_doublebottom[sspt->date_nr] = is_db;
	sspt->mfi[sspt->date_nr] = prev->mfi;
	sspt->date_nr += 1;
}

static int date_is_downtrend(const struct stock_price *price_history, int idx, const struct date_price *price2check)
{
	int i, low_days;
	uint32_t max_down_diff = 0;

	for (i = low_days = 0; i < 15 && idx < price_history->date_cnt; idx++, i++) {
		const struct date_price *prev = &price_history->dateprice[idx];

		if (get_2ndlow(prev) < get_2ndlow(price2check)) {
			low_days += 1;
		}

		if (prev->high > price2check->low && prev->high - price2check->low > max_down_diff)
			max_down_diff = prev->high - price2check->low;
	}

	if (low_days > min_sr_candle_nr || (price2check->low && max_down_diff * 1000 / price2check->low < spt_pullback_margin))
		return 0;

	return 1;
}

static int date_is_uptrend(const struct stock_price *price_history, int idx, const struct date_price *price2check)
{
	int i, low_days;
	uint32_t max_up_diff = 0;
	const struct date_price *lowest_date = NULL;

	for (i = low_days = 0; i < 15 && idx < price_history->date_cnt; idx++, i++) {
		const struct date_price *prev = &price_history->dateprice[idx];

		if (get_2ndhigh(prev) > get_2ndhigh(price2check))
			low_days += 1;

		if (prev->low < price2check->high && price2check->high - prev->low > max_up_diff) {
			max_up_diff = price2check->high - prev->low;
			lowest_date = prev;
		}
	}

	if (low_days > min_sr_candle_nr || !lowest_date || (max_up_diff * 1000 / get_2ndlow(lowest_date) < bo_sr_height_margin))
		return 0;

	return 1;
}

static void check_support(const struct stock_price *price_history, const struct date_price *price2check, struct stock_support *sspt)
{
	const struct date_price *yesterday = NULL;
	const struct date_price *lowest_date = price2check;
	int i;

	sspt->date_nr = 0;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		uint32_t price2check_2ndlow = get_2ndlow(price2check);

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if (yesterday == NULL) {
			yesterday = prev;

			if (!date_is_downtrend(price_history, i, price2check))
				break;

			if (sma2check != -1 && yesterday->sma[sma2check] != 0) {
				if (!sma_hit(price2check->low, yesterday->sma[sma2check])
				    && !sma_hit(price2check_2ndlow, yesterday->sma[sma2check]))
				{
					break;
				}
			}
		}

		if (prev->low < lowest_date->low)
			lowest_date = prev;

		int datecnt = i + 1;
		uint32_t prev_2ndhigh = get_2ndhigh(prev);
		uint32_t prev_2ndlow = get_2ndlow(prev);

		if ((prev->sr_flag & SR_F_SUPPORT_LOW)
		    && sr_height_margin_datecnt(prev->height_low_spt, prev_2ndlow, datecnt))
		{
			if (sr_hit(price2check->low, prev->low) || sr_hit(price2check_2ndlow, prev->low)) {
				int8_t is_db = lowest_date == prev || lowest_date == price2check;
				date2sspt_copy(prev, sspt, is_db);
				continue;
			}
		}

		if ((prev->sr_flag & SR_F_SUPPORT_2ndLOW)
		    && sr_height_margin_datecnt(prev->height_2ndlow_spt, prev_2ndlow, datecnt))
		{
			if (sr_hit(price2check->low, prev_2ndlow) || sr_hit(price2check_2ndlow, prev_2ndlow)) {
				int8_t is_db = lowest_date == prev || lowest_date == price2check;
				date2sspt_copy(prev, sspt, is_db);
				continue;
			}
		}

		if ((prev->sr_flag & SR_F_RESIST_HIGH)
		    && sr_height_margin_datecnt(prev->height_high_rst, prev_2ndhigh, datecnt))
		{
			if (sr_hit(price2check->low, prev->high) || sr_hit(price2check_2ndlow, prev->high)) {
				date2sspt_copy(prev, sspt, 0);
				continue;
			}
		}

		if ((prev->sr_flag & SR_F_RESIST_2ndHIGH)
		    && sr_height_margin_datecnt(prev->height_2ndhigh_rst, prev_2ndhigh, datecnt))
		{
			if (sr_hit(price2check->low, prev_2ndhigh) || sr_hit(price2check_2ndlow, prev_2ndhigh)) {
				date2sspt_copy(prev, sspt, 0);
				continue;
			}
		}
	}
}

static int is_strong_up(const struct stock_price *price_history, const struct date_price *price2check, const struct date_price *prev)
{
	const struct date_price *past;
	int i;

	if (price2check->candle_trend == CANDLE_TREND_BEAR)
		return 0;

	if (!prev) {
		for (i = 0; i < price_history->date_cnt; i++) {
			prev = &price_history->dateprice[i];

			if (strcmp(price2check->date, prev->date) <= 0)
				continue;
			break;
		}

		if (i == price_history->date_cnt)
			return 0;
	}

	int body_size = price2check->close - price2check->open;
	body_size = ((int64_t)body_size) * 1000 / price2check->open;
	int vma10d_inc = prev->vma[VMA_10d] ? price2check->volume * 100 / prev->vma[VMA_10d] : 0;
	int vma20d_inc = prev->vma[VMA_20d] ? price2check->volume * 100 / prev->vma[VMA_20d] : 0;

	if (vma20d_inc >= 200 || vma20d_inc >= 200)
		return 1;

	if (body_size >= 20 && (vma10d_inc >= 120 || vma20d_inc >= 120))
		return 1;

	if (price2check->close > get_2ndlow(prev) && price2check->volume > prev->volume)
		return 1;

	if (price2check->close > prev->high && (vma10d_inc >= 100 || vma20d_inc >= 100))
		return 1;

	for (i = 0, past = prev; i < 5; i++, past = past - 1) {
		if (price2check->volume < past->volume)
			break;
	}

	if (i == 5)
		return 1;

	return 0;
}

static void check_breakout(const struct stock_price *price_history, const struct date_price *price2check, struct stock_support *sspt, int strong)
{
	const struct date_price *yesterday = NULL;
	int i;

	sspt->date_nr = 0;
	sspt->avg_spt_price = 0;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if (yesterday == NULL) {
			yesterday = prev;
			if (!date_is_uptrend(price_history, i, price2check))
				break;

			if (strong && !is_strong_up(NULL, price2check, prev))
				break;
		}

		int datecnt = i + 1;
		uint32_t prev_2ndhigh = get_2ndhigh(prev);
		uint32_t prev_2ndlow = get_2ndlow(prev);

		if ((prev->sr_flag & SR_F_SUPPORT_LOW)
		    && price2check->close > prev->low
		    && yesterday->close <= prev->low
		    && sr_height_margin_datecnt(prev->height_low_spt, prev_2ndlow, datecnt))
		{
			if (bo_hit(price2check->close, prev->low)) {
				date2sspt_copy(prev, sspt, 0);
				sspt->avg_spt_price += (prev->low + prev_2ndlow) >> 1;
				continue;
			}
		}

		if ((prev->sr_flag & SR_F_SUPPORT_2ndLOW)
		    && price2check->close > prev_2ndlow
		    && yesterday->close <= prev_2ndlow
		    && sr_height_margin_datecnt(prev->height_2ndlow_spt, prev_2ndlow, datecnt))
		{
			if (bo_hit(price2check->close, prev_2ndlow)) {
				date2sspt_copy(prev, sspt, 0);
				sspt->avg_spt_price += (prev->low + prev_2ndlow) >> 1;
				continue;
			}
		}

		if ((prev->sr_flag & SR_F_RESIST_HIGH)
		    && price2check->close > prev->high
		    && yesterday->close <= prev->high
		    && sr_height_margin_datecnt(prev->height_high_rst, prev_2ndhigh, datecnt))
		{
			if (bo_hit(price2check->close, prev->high)) {
				date2sspt_copy(prev, sspt, 0);
				sspt->avg_spt_price += (prev->high + prev_2ndhigh) >> 1;
				continue;
			}
		}

		if ((prev->sr_flag & SR_F_RESIST_2ndHIGH)
		    && price2check->close > prev_2ndhigh
		    && yesterday->close <= prev_2ndhigh
		    && sr_height_margin_datecnt(prev->height_2ndhigh_rst, prev_2ndhigh, datecnt))
		{
			if (bo_hit(price2check->close, prev_2ndhigh)) {
				date2sspt_copy(prev, sspt, 0);
				sspt->avg_spt_price += (prev->high + prev_2ndhigh) >> 1;
				continue;
			}
		}
	}

	if (sspt->date_nr)
		sspt->avg_spt_price /= sspt->date_nr;
}

static int get_today_price(const char *symbol, struct date_price *price)
{
	char fname[128];
	char buf[1024];
	FILE *fp;
	int rt = -1;

	snprintf(fname, sizeof(fname), ROOT_DIR "/tmp/%s_today.price", symbol);

	fp = fopen(fname, "r");
	if (!fp)
		return -1;

	fgets(buf, sizeof(buf), fp);

	if (str_to_price(buf, price) < 0)
		goto finish;

	rt = 0;

finish:
	fclose(fp);

	return rt;
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
			//anna_error("date='%s' is  not found in history price\n", date);
			return -1;
		}
	}
	else if (get_today_price(symbol, price2check) == 0) {
		return 0;
	}
	else if (!date || !date[0]) {
		memcpy(price2check, &price_history->dateprice[0], sizeof(*price2check));
	}

	return 0;
}

static int get_symbol_price_for_check(const char *symbol, const char *date, const char *fname,
					struct stock_price *price_history, struct date_price *price2check)
{
	if (stock_price_history_from_file(fname, price_history) < 0) {
		anna_error("stock_price_history_from_file(%s) failed\n", fname);
		return -1;
	}

	if (get_stock_price2check(symbol, date, price_history, price2check) < 0) {
		//anna_error("%s: get_stock_price2check(%s)\n", symbol, date);
		return -1;
	}

	return 0;
}

static void get_250d_high_low(const struct stock_price *price_history, const struct date_price *price2check,
				uint32_t *high, uint32_t *low)
{
	int i, j;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		*high = price2check->high;
		*low = price2check->low;

		for (j = i; j < 250 && j < price_history->date_cnt; j++) {
			prev = &price_history->dateprice[j];
			if (prev->high > *high)
				*high = prev->high;
			if (prev->low < *low)
				*low = prev->low;
		}

		break;
	}
}

static const char * get_price_volume_change(const struct stock_price *price_history, const struct date_price *price2check)
{
	static char output_str[256];
	const struct date_price *yesterday = NULL;
	uint32_t yesterday_2ndhigh;
	int is_up = 0, price_change = 0, vma20d_percent = 0;
	int price_larger_days = 0, count_price = 1, is_first_new_high = 0;
	int volume_larger_days = 0, count_volume = 1;
	int up_tail_percent = 0, body_percent = 0, down_tail_percent = 0, body_size = 0;
	int i;

	output_str[0] = 0;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) > 0) {
			yesterday = prev;
			break;
		}
	}

	if (!yesterday)
		return "N/A";

	yesterday_2ndhigh = get_2ndhigh(yesterday);

	for ( ; i < price_history->date_cnt && i < 250 && (count_volume || count_price); i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (prev->volume == 0)
			continue;

		if (count_volume) {
			if (price2check->volume > prev->volume)
				volume_larger_days += 1;
			else if (price2check->volume < prev->volume)
				count_volume = 0;
		}

		if (count_price) {
			uint32_t prev_2ndhigh = get_2ndhigh(prev);
			if (get_2ndhigh(price2check) > prev_2ndhigh)
				price_larger_days += 1;
			else if (get_2ndhigh(price2check) < prev_2ndhigh)
				count_price = 0;
			if (yesterday_2ndhigh < prev_2ndhigh)
				is_first_new_high = 1;
		}
	}

	if (price2check->volume) {
		if (yesterday->vma[VMA_20d]) {
			vma20d_percent = (uint64_t)price2check->volume * 1000 / yesterday->vma[VMA_20d];
		}
	}

	if (price2check->close && yesterday->close) {
		if (price2check->close >= yesterday->close) {
			price_change = price2check->close - yesterday->close;
			is_up = 1;
		}
		else
			price_change = yesterday->close - price2check->close;

		price_change = ((uint64_t)price_change) * 1000 / yesterday->close;
	}

	if (price2check->low != price2check->high && price2check->open && price2check->close) {
		int price_range = price2check->high - price2check->low;
		int up_tail_range = price2check->high - get_2ndhigh(price2check);
		int body_range = get_2ndhigh(price2check) - get_2ndlow(price2check);
		int down_tail_range = get_2ndlow(price2check) - price2check->low;
		up_tail_percent = up_tail_range * 1000 / price_range;
		body_percent = body_range * 1000 / price_range;
		down_tail_percent = down_tail_range * 1000 / price_range;

                if (price2check->close >= price2check->open)
                        body_size = price2check->close - price2check->open;
                else
                        body_size = price2check->open - price2check->close;

                body_size = ((uint64_t)body_size) * 1000 / price2check->open;
	}

	snprintf(output_str, sizeof(output_str),
		"price(%d.%03d > %d days, %s%c%d.%d%%, color=%s/trend=%s(%d.%d/%d.%d/%d.%d), body_size=%d.%d%%, volume(%d > %s%d%s days, vma20d=%s%d.%d%%%s)",
		price2check->close / 1000, price2check->close % 1000, price_larger_days, (price_larger_days >= 200 && is_first_new_high) ? "1st_new_high, " : "",
		is_up ? '+' : '-', price_change / 10, price_change % 10,
		candle_color[price2check->candle_color], candle_trend[price2check->candle_trend],
		up_tail_percent / 10, up_tail_percent % 10, body_percent / 10, body_percent % 10, down_tail_percent / 10, down_tail_percent % 10,
		body_size / 10, body_size % 10,
		price2check->volume, volume_larger_days >= 5 ? ANSI_COLOR_YELLOW : "", volume_larger_days, volume_larger_days >= 5 ? ANSI_COLOR_RESET : "",
		vma20d_percent >= 1000 ? ANSI_COLOR_YELLOW : "", vma20d_percent / 10, vma20d_percent % 10, vma20d_percent >= 1000 ? ANSI_COLOR_RESET : "");

	return output_str;
}

static void symbol_check_support(const char *symbol, const struct stock_price *price_history,
				 const struct date_price *price2check)
{
	struct stock_support sspt = { };

	check_support(price_history, price2check, &sspt);

	if (!sspt.date_nr)
		return;

	anna_info("%s%-10s%s: date=%s, %s; is supported by %d dates:",
		  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
		  price2check->date, get_price_volume_change(price_history, price2check), sspt.date_nr);

	anna_info("%s<sector=%s>%s.\n", ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

	selected_symbol_nr += 1;
}

static void symbol_check_volume_support(const char *symbol, const struct stock_price *price_history,
					const struct date_price *price2check)
{
	struct stock_support sspt = { };
	const struct date_price *yesterday = NULL;
	int i;

	check_support(price_history, price2check, &sspt);

	if (!sspt.date_nr)
		return;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		yesterday = prev;

		break;
	}

	/* volume > 1.6 times */
	if (!yesterday || price2check->volume*10 < yesterday->vma[VMA_20d]*16)
		return;

	anna_info("%s%-10s%s: date=%s, %s; is supported by %d dates:",
		  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
		  price2check->date, get_price_volume_change(price_history, price2check), sspt.date_nr);

	anna_info("%s<sector=%s>%s.\n", ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

	selected_symbol_nr += 1;
}

static void symbol_check_crawl_sma(const char *symbol, const struct stock_price *price_history,
				 const struct date_price *price2check)
{
	int i, j;
	int count = 0;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) < 0)
			continue;

		for (j = i; j < price_history->date_cnt; j++) {
			prev = &price_history->dateprice[j];

			if (prev->volume && prev->sma[sma2check] != 0
			    && prev->close >= prev->sma[sma2check]
			    && (sma_hit(prev->low, prev->sma[sma2check])
				|| sma_hit(get_2ndlow(prev), prev->sma[sma2check])))
			{
				count += 1;
			}
			else
				break;
		}

		if (count >= 4) {
			anna_info("%s%-10s%s: date=%s, %s; has %d days crawl on SMA%dd; %s<sector=%s>%s.\n",
				ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
				price2check->date, get_price_volume_change(price_history, price2check),
				count, sma2check == SMA_20d ? 20 : 50,
				ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

			selected_symbol_nr += 1;
		}

		break;
	}
}

static int good_up_day(const struct date_price *price2check, const struct date_price *yesterday)
{
	if (price2check->candle_trend == CANDLE_TREND_BEAR)
		return 0;

	if (price2check->high < yesterday->high
	    || price2check->high < (yesterday + 1)->high
	    || price2check->high < (yesterday + 2)->high)
		return 0;

	if ((price2check->high - price2check->close) * 100 / (price2check->high - price2check->low) > 25
	    && (price2check->low < yesterday->high || price2check->low < (yesterday+1)->high || price2check->low < (yesterday+2)->high))
		return 0;

	return 1;
}

static int sma20_slope_is_shallow(const struct date_price *day1)
{
	const struct date_price *day5 = day1 + 5;

	/* sma20 slope needs be shalow: < 1.5% */
	if (day1->sma[SMA_20d] < day5->sma[SMA_20d]
	    && (day5->sma[SMA_20d] - day1->sma[SMA_20d]) * 1000 > day1->sma[SMA_20d] * 15)
		return 0;

	if (day1->sma[SMA_20d] >= day5->sma[SMA_20d]
	    && (day1->sma[SMA_20d] - day5->sma[VMA_20d]) * 1000 >= day5->sma[VMA_20d] * 15)
		return 0;

	return 1;
}

static int is_sma_crossup(const struct date_price *today, const struct date_price *yesterday)
{
	if (today->low < yesterday->sma[sma2check] && today->close > yesterday->sma[sma2check])
		return 1;

	if (today->close > yesterday->sma[sma2check] && today->close < yesterday->sma[sma2check])
		return 1;

	return 0;
}

static void symbol_check_weeks_low_sma(const char *symbol, const struct stock_price *price_history,
					 const struct date_price *price2check)
{
	int i, j;
	uint32_t low_26week = -1;
	int days = 250;

	if (price2check->close < price2check->open || price2check->high == price2check->low)
		return;


	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if (!prev->sma[sma2check])
			return;

		if (!good_up_day(price2check, prev))
			return;

		if (price2check->volume * 100 < prev->vma[VMA_20d] * 115)
			return;

		if (!sma20_slope_is_shallow(prev))
			return;

		if (is_sma_crossup(price2check, prev))
			break;

		return;
	}

	if (weeks2check == 0)
		goto found;
	else if (weeks2check == 26)
		days = 130;
	else if (weeks2check == 13)
		days = 65;

	for (j = 0; i < price_history->date_cnt && j < days; i++, j++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (prev->close < low_26week)
			low_26week = prev->close;
	}

	if (price2check->close < low_26week
	    || (price2check->close - low_26week) * 100 / low_26week > 10)
		return;

found:
	anna_info("%s%-10s%s: date=%s, %s; %s<sector=%s>%s.\n",
		ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
		price2check->date, get_price_volume_change(price_history, price2check),
		ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

	selected_symbol_nr += 1;
}

static void symbol_check_sma_pullback(const char *symbol, const struct stock_price *price_history,
					 const struct date_price *price2check)
{
	const struct date_price *yesterday, *yesterday1, *yesterday2, *yesterday3;
	int i;

	for (i = 0; i < price_history->date_cnt; i++) {
		yesterday = &price_history->dateprice[i];
		if (strcmp(price2check->date, yesterday->date) > 0) {
			yesterday1 = yesterday + 1;
			yesterday2 = yesterday + 2;
			yesterday3 = yesterday + 3;
			break;
		}
	}

	if (i == price_history->date_cnt)
		return;

	if ((yesterday->close >= yesterday->open || yesterday->candle_color == CANDLE_COLOR_DOJI)
	    && yesterday->high > yesterday->sma[sma2check]
	    && yesterday->volume > yesterday->vma[VMA_20d]
	    && yesterday1->close < yesterday1->sma[sma2check]
	    && price2check->close < yesterday->sma[sma2check])
	{
		goto is_pb;
	}

	if (yesterday1->close > yesterday1->open
	    && is_sma_crossup(yesterday1, yesterday2)
	    && yesterday1->volume > yesterday1->vma[VMA_20d]
	    && get_2ndhigh(price2check) < get_2ndhigh(yesterday1))
	{
		goto is_pb;
	}

	if (yesterday2->close > yesterday2->open
	    && is_sma_crossup(yesterday2, yesterday3)
	    && yesterday2->volume > yesterday2->vma[VMA_20d]
	    && get_2ndhigh(price2check) < get_2ndhigh(yesterday2))
	{
		goto is_pb;
	}


	return;

is_pb:
	anna_info("%s%-10s%s: date=%s, %s; %s<sector=%s>%s.\n",
		ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
		price2check->date, get_price_volume_change(price_history, price2check),
		ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

	selected_symbol_nr += 1;
}

static int get_date_count(const struct stock_price *price_history, const char *date1, const char *date2)
{
	const char *date_smaller, *date_bigger;
	int cmp = strcmp(date1, date2);
	int i, j;

	cmp = strcmp(date1, date2);
	if (cmp == 0)
		return 0;

	if (cmp > 0) {
		date_smaller = date2;
		date_bigger = date1;
	}
	else {
		date_smaller = date1;
		date_bigger = date2;
	}

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *cur = &price_history->dateprice[i];
		if (strcmp(cur->date, date_bigger) > 0)
			continue;

		for (j = i; j < price_history->date_cnt; j++) {
			cur = &price_history->dateprice[j];
			if (strcmp(cur->date, date_smaller) == 0)
				break;
		}

		return j - i;
	}

	return 0;
}

static int datecnt_match_check_pullback(int check_pullback, int datecnt)
{
	if (check_pullback) {
		if (datecnt > 25 || datecnt < 10)
			return 0;
	}
	else {
		if (datecnt <= 25 && datecnt >= 10)
			return 0;
	}

	return 1;
}

static void __symbol_check_doublebottom(const char *symbol, const struct stock_price *price_history,
					const struct date_price *price2check, int check_pullback)
{
	struct stock_support sspt = { };
	int i;

	check_support(price_history, price2check, &sspt);

	if (!sspt.date_nr)
		return;

	for (i = 0; i < sspt.date_nr; i++) {
		if (sspt.is_doublebottom[i]) {
			int datecnt = get_date_count(price_history, sspt.date[i], price2check->date);

			if (!datecnt_match_check_pullback(check_pullback, datecnt))
				return;

			anna_info("%s%-10s%s: date=%s/%s(%d days), %s; %s<sector=%s>%s.\n",
				ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET, price2check->date, sspt.date[i], datecnt,
				get_price_volume_change(price_history, price2check),
				ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

			selected_symbol_nr += 1;

			break;
		}
	}
}

static void symbol_check_doublebottom(const char *symbol, const struct stock_price *price_history,
					const struct date_price *price2check)
{
	__symbol_check_doublebottom(symbol, price_history, price2check, 0);
}

static void symbol_check_pullback_doublebottom(const char *symbol, const struct stock_price *price_history,
						const struct date_price *price2check)
{
	__symbol_check_doublebottom(symbol, price_history, price2check, 1);
}

static void symbol_check_mfi_doublebottom(const char *symbol, const struct stock_price *price_history,
					const struct date_price *price2check)
{
	struct stock_support sspt = { };
	const struct date_price *prev;
	int i;

	check_support(price_history, price2check, &sspt);

	if (!sspt.date_nr)
		return;

	for (i = 0; i < price_history->date_cnt; i++) {
		prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) > 0)
			break;
	}

	if (i == price_history->date_cnt)
		return;

	if (!sma20_slope_is_shallow(prev))
		return;

	for (i = 0; i < sspt.date_nr; i++) {
		uint32_t sspt_mfi = sspt.mfi[i] ? sspt.mfi[i] : 1;
		uint32_t high_250d, low_250d;
		int low_250d_percent = 0;
		int mfi_diff_percent;

		if (prev->mfi <= sspt_mfi)
			continue;

		get_250d_high_low(price_history, price2check, &high_250d, &low_250d);
		if (price2check->low < low_250d)
			printf("[%s:%s:%d] date=%s's low is larger than 250d_low: %d/%d\n",
				__FILE__, __FUNCTION__, __LINE__, price2check->date, price2check->low, low_250d);
		else {
			low_250d_percent = (price2check->low - low_250d) * 100 / low_250d;
		}

		mfi_diff_percent = (prev->mfi - sspt_mfi) * 100 / sspt_mfi;

		if (sspt.is_doublebottom[i]
		    && (mfi_diff_percent >= 100 ||
			(low_250d_percent <= 15 && mfi_diff_percent >= 50 && prev->mfi <= 5500 && sspt_mfi <= 4500)))
		{
			uint32_t diff_mfi = prev->mfi - sspt.mfi[i];
			anna_info("%s%-10s%s: date=%s/%s, %s; MFI(%d.%02d/%d.%02d=%d.%02d%%); %s<sector=%s>%s.\n",
				ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET, price2check->date, sspt.date[i],
				get_price_volume_change(price_history, price2check),
				prev->mfi / 100, prev->mfi % 100, sspt.mfi[i] / 100, sspt.mfi[i] % 100,
				diff_mfi * 100 / sspt_mfi, diff_mfi * 100 % sspt_mfi,
				ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

			selected_symbol_nr += 1;

			break;
		}
	}
}

static void __symbol_check_doublebottom_up(const char *symbol, const struct stock_price *price_history,
					const struct date_price *price2check, int check_pullback, int strong)
{
	const struct date_price *prev;
	struct stock_support sspt = { };
	int i;

	for (i = 0; i < price_history->date_cnt; i++) {
		prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) > 0)
			break;
	}

	if (i == price_history->date_cnt)
		return;

	if (price2check->high == price2check->low
	    || ((price2check->high - price2check->close) * 100 / (price2check->high - price2check->low) >= 25))
		return;

	if (strong && !is_strong_up(price_history, price2check, NULL))
		return;
#if 0
	if (!sma20_slope_is_shallow(prev))
		return;
#endif

	if (price2check->volume * 133 < prev->vma[VMA_20d] * 100)
		return;

	int use_today = (price2check->close < prev->close || (price2check->low < prev->low && get_2ndlow(price2check) < get_2ndlow(prev))) && price2check->candle_trend != CANDLE_TREND_BEAR;

	check_support(price_history, use_today ? price2check : prev, &sspt);

	for (i = 0; i < sspt.date_nr; i++) {
		if (sspt.is_doublebottom[i]) {
			int datecnt = get_date_count(price_history, sspt.date[i], use_today ? price2check->date : prev->date);

			if (check_pullback && !datecnt_match_check_pullback(check_pullback, datecnt))
				return;

			anna_info("%s%-10s%s: date=%s/%s(%d days), %s; %s<sector=%s>%s.\n",
					ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET, use_today ? price2check->date : prev->date, sspt.date[i], datecnt,
					get_price_volume_change(price_history, price2check),
					ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

			selected_symbol_nr += 1;

			break;
		}
	}
}

static void symbol_check_doublebottom_up(const char *symbol, const struct stock_price *price_history,
					const struct date_price *price2check)
{
	__symbol_check_doublebottom_up(symbol, price_history, price2check, 0, 0);
}

static void symbol_check_pullback_doublebottom_up(const char *symbol, const struct stock_price *price_history,
						const struct date_price *price2check)
{
	__symbol_check_doublebottom_up(symbol, price_history, price2check, 1, 0);
}

static void symbol_check_strong_doublebottom_up(const char *symbol, const struct stock_price *price_history,
						const struct date_price *price2check)
{
	__symbol_check_doublebottom_up(symbol, price_history, price2check, 0, 1);
}

static void symbol_check_pullback(const char *symbol, const struct stock_price *price_history,
				  const struct date_price *price2check)
{
	const struct date_price *yesterday = NULL;
	int i, j;

	for (i = 0; i < price_history->date_cnt; i++) {
		yesterday = &price_history->dateprice[i];

		if (strcmp(price2check->date, yesterday->date) > 0)
			break;
	}

	for (j = 0; j < 20 && i < price_history->date_cnt; i++, j++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (!(prev->sr_flag & SR_F_BIGUPDAY))
			continue;

		uint32_t prev_2ndlow = get_2ndlow(prev);
		uint32_t price2check_2ndlow = get_2ndlow(price2check);

		if (sr_hit(price2check->low, prev->low) || sr_hit(price2check_2ndlow, prev->low)
		    || sr_hit(price2check->low, prev_2ndlow) || sr_hit(price2check_2ndlow, prev_2ndlow))
		{
			anna_info("%s%-10s%s: date=%s, %s; is at support bigupdate=%s; %s<sector=%s>%s.\n",
				  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
				  price2check->date, get_price_volume_change(price_history, price2check), prev->date,
				  ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

			selected_symbol_nr += 1;

			break;
		}
	}
}

static void __symbol_check_breakout(const char *symbol, const struct stock_price *price_history,
				 const struct date_price *price2check, int strong, int second_bo)
{
	struct stock_support sspt = { };
	int i, j;

	check_breakout(price_history, price2check, &sspt, strong);

	if (!sspt.date_nr)
		return;

	if (second_bo) {
		for (i = 0; i < price_history->date_cnt; i++) {
			const struct date_price *prev = &price_history->dateprice[i];
			if (strcmp(price2check->date, prev->date) <= 0)
				continue;

			for (j = 0; j < 5 && i < price_history->date_cnt; i++, j++) {
				const struct date_price *prev = &price_history->dateprice[i];
				if (prev->high >= sspt.avg_spt_price)
					break;
			}

			if (j < 5)
				break;
			else
				return;
		}
	}

	anna_info("%s%-10s%s: date=%s, %s; breakout with %d dates:",
		  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
		  price2check->date, get_price_volume_change(price_history, price2check), sspt.date_nr);
#if 0
	for (i = 0; i < sspt.date_nr; i++)
		anna_info(" %s(%c)", sspt.date[i], is_support(sspt.sr_flag[i]) ? 's' : is_resist(sspt.sr_flag[i]) ? 'r' : '?');
#endif

	anna_info("%s<sector=%s>%s.\n", ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

	selected_symbol_nr += 1;
}

static void symbol_check_breakout(const char *symbol, const struct stock_price *price_history,
				 const struct date_price *price2check)
{
	__symbol_check_breakout(symbol, price_history, price2check, 0, 0);
}

static void symbol_check_2nd_breakout(const char *symbol, const struct stock_price *price_history,
				 const struct date_price *price2check)
{
	__symbol_check_breakout(symbol, price_history, price2check, 0, 1);
}

static int date_is_trend_breakout(const char *symbol, const struct stock_price *price_history,
				const struct date_price *price2check)
{
	const struct date_price *yesterday = NULL;
	uint32_t recent_high;
	int i;

	if (price2check->candle_trend == CANDLE_TREND_BEAR || price2check->high == price2check->low)
		return 0;

	for (i = 0; i < price_history->date_cnt; i++) {
		yesterday = &price_history->dateprice[i];

		if (strcmp(price2check->date, yesterday->date) > 0)
			break;
	}

	if (i == price_history->date_cnt)
		return 0;

	if (!good_up_day(price2check, yesterday))
		return 0;

	if (price2check->volume * 100 < yesterday->vma[VMA_20d] * 115)
		return 0;

	if (!sma20_slope_is_shallow(yesterday))
		return 0;

	recent_high = get_2ndhigh(price2check);

	for (i = 0; i < 40; i++) {
		const struct date_price *prev = yesterday + i;
		if (get_2ndhigh(prev) > recent_high)
			recent_high = get_2ndhigh(prev);
	}

	/* diff from recent_high < 5% */
	if ((recent_high - get_2ndhigh(price2check)) * 1000 / get_2ndhigh(price2check) <= 50)
		return 0;

	return 1;
}

static void symbol_check_trend_breakout(const char *symbol, const struct stock_price *price_history,
					 const struct date_price *price2check)
{
	const struct date_price *yesterday = NULL;
	int i;

	if (price2check->candle_trend == CANDLE_TREND_BEAR || price2check->high == price2check->low)
		return;

	for (i = 0; i < price_history->date_cnt; i++) {
		yesterday = &price_history->dateprice[i];

		if (strcmp(price2check->date, yesterday->date) > 0)
			break;
	}

	if (i == price_history->date_cnt)
		return;

	if (date_is_trend_breakout(symbol, price_history, yesterday))
		return;

	if (!date_is_trend_breakout(symbol, price_history, price2check))
		return;

	anna_info("%s%-10s%s: date=%s, %s; %s.\n",
		ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
		price2check->date, get_price_volume_change(price_history, price2check),
		price_history->sector);

	selected_symbol_nr += 1;
}

static void symbol_check_strong_uptrend(const char *symbol, const struct stock_price *price_history,
					 const struct date_price *price2check)
{
	const struct date_price *yesterday = NULL;
	int days_below_sma20 = -1;
	int i, j;

	for (i = 0, j = 0; i < price_history->date_cnt && j < 25; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) >0) {
			if (yesterday == NULL)
				yesterday = prev;

			if (prev->close < prev->sma[SMA_20d]) {
				if (days_below_sma20 < 0)
					days_below_sma20 = 0;
				days_below_sma20 += 1;
			}
			j += 1;
		}
	}

	if (days_below_sma20 >= 0 && days_below_sma20 <= 4
	    && (price2check->close > yesterday->sma[SMA_20d] && (price2check->low < yesterday->sma[SMA_20d] || yesterday->close < yesterday->sma[SMA_20d])))
	{
		anna_info("%s%-10s%s: date=%s, days_below_sma20=%d, %s; %s<sector=%s>%s.\n",
			ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
			price2check->date, days_below_sma20, get_price_volume_change(price_history, price2check),
			ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);
		selected_symbol_nr += 1;
	}
}

static void __symbol_check_strong_breakout(const char *symbol, const struct stock_price *price_history,
					 const struct date_price *price2check, int strong_body)
{
#define STRONG_BO_MAX_DAYS   5
	uint32_t price2check_2ndhigh = get_2ndhigh(price2check);
	const struct date_price *yesterday = NULL;
	uint32_t price2check_2ndlow = get_2ndlow(price2check);
	uint32_t low_40day = price2check_2ndlow;
	uint32_t high_40day = price2check_2ndhigh;
	int i, j;

	/* must be bull trend bar */
	if (price2check->candle_trend == CANDLE_TREND_BEAR && price2check->close < price2check->open)
		return;

	for (i = j = 0; i < price_history->date_cnt && j < 40; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) > 0) {
			if (prev->volume == 0)
				continue;

			if (yesterday == NULL) {
				yesterday = prev;
				/* if not strong_body, volume needs be enough */
				if (!strong_body && price2check->volume < prev->vma[VMA_20d])
					return;
				if (strong_body && ((uint64_t)price2check->volume * 100) < ((uint64_t)prev->vma[VMA_20d] * 75))
					return;
			}

			uint32_t prev_2ndhigh = get_2ndhigh(prev);
			uint32_t prev_2ndlow = get_2ndlow(prev);

			if (prev_2ndlow < low_40day)
				low_40day = prev_2ndlow;

			if (prev_2ndhigh > high_40day)
				high_40day = prev_2ndhigh;

			j += 1;
		}
	}

	if (high_40day == price2check_2ndhigh
	    || (price2check_2ndlow > low_40day && (price2check_2ndlow - low_40day) * 100 / low_40day > 10))
		return;

	if (strong_body) {
		uint64_t body_size;

		if (price2check->candle_trend == CANDLE_TREND_DOJI)
			return;

                if (price2check->close >= price2check->open)
                        body_size = price2check->close - price2check->open;
                else
                        body_size = price2check->open - price2check->close;

		if (body_size * 1000 / price2check->open < 10) /* body size >= 1% */
			return;
	}

	for (i = j = 0; i < price_history->date_cnt && j < STRONG_BO_MAX_DAYS; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) > 0) {
			if (prev->volume == 0)
				continue;

			uint64_t prev_2ndhigh = get_2ndhigh(prev);
			uint64_t price2check_2ndlow = get_2ndlow(price2check);
			if (price2check_2ndhigh < prev->high
			    || (prev_2ndhigh > price2check_2ndlow
				&& (prev_2ndhigh - price2check_2ndlow) * 10000 / prev_2ndhigh >= 70))
			{
				break;
			}
			j += 1;
		}
	}

	if (j < STRONG_BO_MAX_DAYS)
		return;

	anna_info("%s%-10s%s: date=%s, %s; %s.\n",
		ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
		price2check->date, get_price_volume_change(price_history, price2check),
		price_history->sector);

	selected_symbol_nr += 1;
}

static void symbol_check_strong_breakout(const char *symbol, const struct stock_price *price_history,
					 const struct date_price *price2check)
{
	__symbol_check_strong_breakout(symbol, price_history, price2check, 0);
}

static void symbol_check_strong_body_breakout(const char *symbol, const struct stock_price *price_history,
					 const struct date_price *price2check)
{
	__symbol_check_strong_breakout(symbol, price_history, price2check, 1);
}

static void symbol_check_mfi(const char *symbol, const struct stock_price *price_history,
				 const struct date_price *price2check)
{
	const struct date_price *yesterday, *prev_20d;
	int i;

	for (i = 0; i < price_history->date_cnt; i++) {
		yesterday = &price_history->dateprice[i];
		if (strcmp(price2check->date, yesterday->date) > 0) {
			prev_20d = yesterday + 20;
			break;
		}
	}

	if (yesterday->mfi > prev_20d->mfi && prev_20d->mfi
	    && (yesterday->mfi - prev_20d->mfi) * 100 / (prev_20d->mfi >= 0 ? prev_20d->mfi : -prev_20d->mfi) >= 25
	    && yesterday->close < prev_20d->close
	    && price2check->volume * 100 / yesterday->vma[VMA_20d] <= 60)
	{
		anna_info("%s%-10s%s: date=%s, %s; %s.\n",
				ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
				price2check->date, get_price_volume_change(price_history, price2check),
				price_history->sector);

		selected_symbol_nr += 1;
	}
}

static void symbol_check_early_up(const char *symbol, const struct stock_price *price_history,
				    const struct date_price *price2check)
{
	int i;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if ((price2check->close - prev->close) * 1000 / prev->close >= 25) {
			anna_info("%s%-10s%s: date=%s, %s, is early up from yesterday date=%s.\n",
				  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET, price2check->date,
				  get_price_volume_change(price_history, price2check), prev->date);

			selected_symbol_nr += 1;
		}

		break;
	}
}

static void symbol_check_52w_low_up(const char *symbol, const struct stock_price *price_history,
				    const struct date_price *price2check)
{
	const struct date_price *yesterday = NULL;
	uint32_t price2check_2ndlow = get_2ndlow(price2check);
	uint32_t yesterday_2ndlow;
	uint32_t is_52w_low = 1, is_52w_2ndlow = 1;
	int i, j;

	for (i = 0; i < price_history->date_cnt; i++) {
		yesterday = &price_history->dateprice[i];

		if (strcmp(price2check->date, yesterday->date) <= 0)
			continue;

		yesterday_2ndlow = get_2ndlow(yesterday);

		if (price2check->low < yesterday->low && price2check_2ndlow < yesterday_2ndlow) {
			is_52w_low = is_52w_2ndlow = 0;
			break;
		}

		for (j = i + 1; j < price_history->date_cnt && (j - i) <= 250; j++) {
			const struct date_price *prev = &price_history->dateprice[j];

			if (prev->low < yesterday->low)
				is_52w_low = 0;
			if (get_2ndlow(prev) < yesterday_2ndlow)
				is_52w_2ndlow = 0;

			if (!is_52w_low && !is_52w_2ndlow)
				break;
		}

		break;
	}

	if ((is_52w_low || is_52w_2ndlow)
	    && (get_2ndhigh(price2check) > get_2ndhigh(yesterday)
	        || (price2check->low >= yesterday->low && price2check_2ndlow >= yesterday_2ndlow))
	   )
	{
		anna_info("%s%-10s%s: date=%s, %s, is up from 52w low date=%s.\n",
			ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET, price2check->date,
			get_price_volume_change(price_history, price2check), yesterday->date);

		selected_symbol_nr += 1;
	}
}

static void get_week_price(const struct stock_price *price_history, int *idx, struct date_price *week_price)
{
	int j;
	int last_wday = 6;

	memset(week_price, 0, sizeof(*week_price));
	week_price->low = (uint32_t)-1;

	for (j = 0; j < 5 && *idx < price_history->date_cnt; (*idx)++, j++) {
		const struct date_price *cur = &price_history->dateprice[*idx];

		if (cur->wday >= last_wday)
			break;

		if (j == 0) {
			week_price->close = cur->close;
			last_wday = cur->wday;
		}

		if (cur->high > week_price->high)
			week_price->high = cur->high;

		if (cur->low < week_price->low)
			week_price->low = cur->low;

		week_price->open = cur->open;
	}
}

static void symbol_check_weekup(const char *symbol, const struct stock_price *price_history,
				 const struct date_price *price2check)
{
	struct date_price w1_price, w2_price;
	int idx = 0;

	get_week_price(price_history, &idx, &w1_price);
	get_week_price(price_history, &idx, &w2_price);

	if (w1_price.close > w1_price.open
	    && w2_price.close > w2_price.open
	    && w1_price.close > w2_price.close
	    && (w2_price.close - w2_price.open) * 100 / (w2_price.high - w2_price.low) >= 70 /* w2 body size >= 70% */
	    && ((w2_price.close - w2_price.open) * 100 / w2_price.open >= 5
		|| (w1_price.close - w1_price.open) * 100 / w1_price.open >= 5))
	{
		anna_info("%s%-10s%s: has continuous 2 weeks uptrend. %s<sector=%s>%s\n",
			ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
			ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

		selected_symbol_nr += 1;
	}
}

static void symbol_check_week_reverse(const char *symbol, const struct stock_price *price_history,
					 const struct date_price *price2check)
{
	struct date_price w1_price, w2_price;
	int idx = 0;

	get_week_price(price_history, &idx, &w1_price);
	get_week_price(price_history, &idx, &w2_price);

	if (w2_price.close < w2_price.open /* last week is down */
	    && w1_price.close > w1_price.open /* this week is up */
	    && w1_price.close > w2_price.open)
	{
		anna_info("%s%-10s%s: has week-reverse. %s<sector=%s>%s\n",
			  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
			  ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

		selected_symbol_nr += 1;
	}
}

static void symbol_check_low_volume(const char *symbol, const struct stock_price *price_history,
				    const struct date_price *price2check)
{
	int i;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if (price2check->volume == 0 || prev->vma[VMA_10d] == 0)
			break;

		int volume_change = (uint64_t)price2check->volume * 1000 / prev->vma[VMA_10d];

		if (price2check->candle_trend == CANDLE_TREND_DOJI
		    && volume_change <= 500)
		{
			anna_info("%s%-10s%s: date=%s, %s; has low volume; %s<sector=%s>%s.\n",
				ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
				price2check->date, get_price_volume_change(price_history, price2check),
				ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);
			selected_symbol_nr += 1;
		}

		break;
	}
}

static int get_less_volume_days(const struct stock_price *price_history,
				const struct date_price *price2check,
				const struct date_price *prev)
{
	const struct date_price *last = &price_history->dateprice[price_history->date_cnt - 1];
	int days;

	for (days = 0; days < 20 && prev < last; prev++, days++) {
		if (price2check->volume <= prev->volume)
			break;
	}

	return days;
}

static void symbol_check_low_volume_up(const char *symbol, const struct stock_price *price_history,
					const struct date_price *price2check)
{
	int i;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if (price2check->close <= prev->close
		    || !prev->vma[VMA_10d]
		    || !prev->volume)
			break;

		int up_change = (price2check->close - prev->close) * 1000 / prev->close;
		int volume_change = (uint64_t)prev->volume * 1000 / prev->vma[VMA_10d];
		int body_size = (price2check->open - price2check->close) * 100 / price2check->open;

		if (price2check->candle_color == CANDLE_COLOR_GREEN /* today is up */
		    && up_change >= 25 /* up >= 2.5% from yesterday */
		    && volume_change <= 650 /* yesterday volume <= 65% vma_10d */
		    && body_size >= 70) /* today has good size body */
		{
			anna_info("%s%-10s%s: date=%s is up from low-volume-date=%s, %s, volume=%u is larger than previous %s%d%s days; %s<sector=%s>%s.\n",
				ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
				price2check->date, prev->date,
				get_price_volume_change(price_history, price2check), price2check->volume,
				ANSI_COLOR_YELLOW, get_less_volume_days(price_history, price2check, prev), ANSI_COLOR_RESET,
				ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

			selected_symbol_nr += 1;
		}

		break;
	}
}

static void symbol_check_volume_up(const char *symbol, const struct stock_price *price_history,
				const struct date_price *price2check)
{
	int i;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if (price2check->close <= prev->close
		    || !price2check->volume
		    || !prev->vma[VMA_10d])
			break;

		int up_change = (price2check->close - prev->close) * 1000 / prev->close;
		int volume_change = (uint64_t)price2check->volume * 1000 / prev->vma[VMA_10d];
		int body_size = (price2check->open - price2check->close) * 100 / price2check->open;

		if (price2check->candle_color == CANDLE_COLOR_GREEN /* today is up */
		    && up_change >= 25 /* up >= 2.5% from yesterday */
		    && volume_change >= 1000 /* >= 100% */
		    && body_size >= 70) /* today has good size body */
		{
			anna_info("%s%-10s%s: date=%s is up with large volume, %s, volume=%u is larger than previous %s%d%s days; %s<sector=%s>%s.\n",
				ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
				price2check->date, get_price_volume_change(price_history, price2check), price2check->volume,
				ANSI_COLOR_YELLOW, get_less_volume_days(price_history, price2check, prev), ANSI_COLOR_RESET,
				ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

			selected_symbol_nr += 1;
		}

		break;
	}
}

static void get_52w_low(const struct stock_price *price_history, const struct date_price *price2check,
			uint32_t *low, uint32_t *second_low)
{
	int i, count;

	*low = *second_low = (uint32_t)-1;

	for (i = 0, count = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if (count >= 250)
			break;

		count += 1;

		if (prev->low < *low)
			*low = prev->low;
		if (get_2ndlow(prev) < *second_low)
			*second_low = get_2ndlow(prev);
	}
}

static int near_52w_low(const struct date_price *price2check, uint32_t low_52w, uint32_t second_low_52w)
{
	uint32_t diff;
	uint32_t price2check_2ndlow;

	if (price2check->low > low_52w)
		diff = price2check->low - low_52w;
	else
		diff = low_52w - price2check->low;
	if (diff * 1000 / low_52w <= 50)
		return 1;

	if (price2check->low > second_low_52w)
		diff = price2check->low - second_low_52w;
	else
		diff = second_low_52w - price2check->low;
	if (diff * 1000 / second_low_52w <= 50)
		return 1;

	price2check_2ndlow = get_2ndlow(price2check);

	if (price2check_2ndlow > low_52w)
		diff = price2check_2ndlow - low_52w;
	else
		diff = low_52w - price2check_2ndlow;
	if (diff * 1000 / low_52w <= 50)
		return 1;

	if (price2check_2ndlow > second_low_52w)
		diff = price2check_2ndlow - second_low_52w;
	else
		diff = second_low_52w - price2check_2ndlow;
	if (diff * 1000 / second_low_52w <= 50)
		return 1;

	return 0;
}

static void symbol_check_52w_doublebottom(const char *symbol, const struct stock_price *price_history,
					const struct date_price *price2check)
{
	struct stock_support sspt = { };
	int i;

	check_support(price_history, price2check, &sspt);

	if (!sspt.date_nr)
		return;

	for (i = 0; i < sspt.date_nr; i++) {
		if (sspt.is_doublebottom[i]) {
			uint32_t low_52w, second_low_52w;

			get_52w_low(price_history, price2check, &low_52w, &second_low_52w);

			if (near_52w_low(price2check, low_52w, second_low_52w)) {
				anna_info("%s%-10s%s: date=%s, %s; is double bottom with dates=%s; %s<sector=%s>%s.\n",
					ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET, price2check->date,
					get_price_volume_change(price_history, price2check), sspt.date[i],
					ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);

				selected_symbol_nr += 1;
			}

			break;
		}
	}
}

static void symbol_check_52w_doublebottom_up(const char *symbol, const struct stock_price *price_history,
                                        const struct date_price *price2check)
{
	int i, cnt = 0;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];
		if (strcmp(prev->date, price2check->date) >= 0)
			continue;

		if (cnt >= 2)
			break;

		cnt += 1;

		if (price2check->close < prev->close)
			continue;

		int saved = selected_symbol_nr;

		symbol_check_52w_doublebottom(symbol, price_history, price2check);

		if (selected_symbol_nr > saved)
			break;
	}
}

static void symbol_check_change(const char *symbol, const struct stock_price *price_history,
				const struct date_price *price2check)
{
	const struct date_price *prev;
	int i;

	for (i = 0; i < price_history->date_cnt; i++) {
		prev = &price_history->dateprice[i];
		if (strcmp(price2check->date, prev->date) > 0)
			break;
	}

	anna_info("%s%-10s%s: date=%s, %s.\n", ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
		  price2check->date, get_price_volume_change(price_history, price2check));
}

static int call_check_func(const char *symbol, const char *date, const char *fname,
			    void (*check_func)(const char *, const struct stock_price *, const struct date_price *))
{
	struct stock_price price_history;
	struct date_price price2check;

	if (get_symbol_price_for_check(symbol, date, fname, &price_history, &price2check) < 0) {
		//anna_error("get_symbol_price_for_check(symbol=%s,date=%s,fname=%s) failed\n", symbol, date, fname);
		return -1;
	}

	check_func(symbol, &price_history, &price2check);

	return 0;
}

static void stock_price_check(const char *group, const char *date, int symbols_nr, const char **symbols,
				void (*check_func)(const char *symbol, const struct stock_price *price_history, const struct date_price *price2check))
{

	char path[128];
	char fname[256];
	int i;

	snprintf(path, sizeof(path), "%s/%s", ROOT_DIR, group);

	selected_symbol_nr = 0;

	if (symbols_nr) {
		for (i = 0; i < symbols_nr; i++) {
			snprintf(fname, sizeof(fname), "%s/%s.price", path, symbols[i]);

			call_check_func(symbols[i], date, fname, check_func);
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

			call_check_func(symbol, date, fname, check_func);
		}

		closedir(dir);
	}

	anna_info("%s%d%s symbols are selected.\n", ANSI_COLOR_YELLOW, selected_symbol_nr, ANSI_COLOR_RESET);
}

void stock_price_check_support(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_support);
}

void stock_price_check_volume_support(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_volume_support);
}

void stock_price_check_sma(const char *group, const char *date, int sma_idx, int symbols_nr, const char **symbols)
{
	sma2check = sma_idx;
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_support);
	sma2check = -1;
}

void stock_price_check_crawl_sma(const char *group, const char *date, int sma_idx, int symbols_nr, const char **symbols)
{
	sma2check = sma_idx;
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_crawl_sma);
	sma2check = -1;
}


void stock_price_check_weeks_low_sma(const char *group, const char *date, int weeks, int sma_idx, int symbols_nr, const char **symbols)
{
	weeks2check = weeks;
	sma2check = sma_idx;
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_weeks_low_sma);
	sma2check = -1;
	weeks2check = 0;
}

void stock_price_check_sma_pullback(const char *group, const char *date, int sma_idx, int symbols_nr, const char **symbols)
{
	sma2check = sma_idx;
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_sma_pullback);
	sma2check = -1;
}

void stock_price_check_doublebottom(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_doublebottom);
}

void stock_price_check_mfi_doublebottom(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_mfi_doublebottom);
}

void stock_price_check_pullback_doublebottom(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_pullback_doublebottom);
}

void stock_price_check_52w_doublebottom(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_52w_doublebottom);
}

void stock_price_check_52w_doublebottom_up(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_52w_doublebottom_up);
}

void stock_price_check_doublebottom_up(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_doublebottom_up);
}

void stock_price_check_pullback_doublebottom_up(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_pullback_doublebottom_up);
}

void stock_price_check_strong_doublebottom_up(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_strong_doublebottom_up);
}

void stock_price_check_pullback(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_pullback);
}

void stock_price_check_breakout(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_breakout);
}

void stock_price_check_2nd_breakout(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_2nd_breakout);
}

void stock_price_check_trend_breakout(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_trend_breakout);
}

void stock_price_check_strong_uptrend(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_strong_uptrend);
}

void stock_price_check_strong_breakout(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_strong_breakout);
}

void stock_price_check_strong_body_breakout(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_strong_body_breakout);
}

void stock_price_check_mfi(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_mfi);
}

void stock_price_check_early_up(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_early_up);
}

void stock_price_check_52w_low_up(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_52w_low_up);
}

void stock_price_check_weekup(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_weekup);
}

void stock_price_check_week_reverse(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_week_reverse);
}

void stock_price_check_low_volume(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_low_volume);
}

void stock_price_check_low_volume_up(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_low_volume_up);
}

void stock_price_check_volume_up(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_volume_up);
}

void stock_price_check_change(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_change);
}
