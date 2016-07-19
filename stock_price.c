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

	if (cur->sr_flag)
		return;

	/* check for big up day */
	if (cur->candle_color == CANDLE_COLOR_GREEN /* today is an up day */
	    && (cur->high - cur->low) * 100 / cur->low >= 7 /* up >= 7% */
	    && (cur->close - cur->open) * 100 / (cur->high - cur->low) >= 70 /* body size >= 70% */
	    && (cur - 1)->close < cur->high)  /* next day is down day */
	{
		cur->sr_flag = SR_F_BIGUPDAY;
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

	price->sector[0] = 0;

	while (fgets(buf, sizeof(buf), fp)) {
		char *token;

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

		struct date_price *cur = &price->dateprice[price->date_cnt];

		token = strtok(buf, ",");
		if (!token) continue;
		strlcpy(cur->date, token, sizeof(cur->date));

		token = strtok(NULL, ",");
		if (!token) continue;
		cur->wday = atoi(token);

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

static int str2date(const char *date, int *year, int *month, int *mday)
{
	char _date[32];
	char *token, *saved;

	strlcpy(_date, date, sizeof(_date));

	token = strtok_r(_date, "-", &saved);
	if (!token) return -1;
	*year = atoi(token);

	token = strtok_r(NULL, "-", &saved);
	if (!token) return -1;
	*month = atoi(token);

	token = strtok_r(NULL, "-", &saved);
	if (!token) return -1;
	*mday = atoi(token);

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
		else
			cur->wday = dayofweek(year, month, mday);

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

	for (i = 0; i < price->date_cnt; i++) {
		const struct date_price *p = &price->dateprice[i];

		fprintf(fp,
			"%s,%u,%u,%u,%u,%u,%u," /* date, wday, open, high, low, close, volume */
			"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u," /* sma_10/20/30/50/60/100/120/200d, vma_10/20/60d */
			"%u,%u,%u,%u,%u,%u,%u\n",
			p->date, p->wday, p->open, p->high, p->low, p->close, p->volume,
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
	int8_t  is_doublebottom[STOCK_SUPPORT_MAX_DATES];
	int date_nr;
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
	sspt->date_nr += 1;
}

static int date_is_downtrend(const struct stock_price *price_history, int idx, const struct date_price *price2check)
{
	int candle_falling_nr;
	uint32_t max_down_diff = 0;
	int is_falling = 1;

	for (candle_falling_nr = 0; candle_falling_nr < max_sr_candle_nr && idx < price_history->date_cnt; idx++, candle_falling_nr++) {
		const struct date_price *prev = &price_history->dateprice[idx];

		if (prev->low < price2check->low) {
			if (candle_falling_nr < min_sr_candle_nr - 1)
				is_falling = 0;
			break;
		}

		if (prev->high > price2check->low && prev->high - price2check->low > max_down_diff)
			max_down_diff = prev->high - price2check->low;
	}

	if (!is_falling || (max_down_diff * 1000 / get_2ndlow(price2check) < spt_pullback_margin))
		return 0;

	return 1;
}

static int date_is_uptrend(const struct stock_price *price_history, int idx, const struct date_price *price2check)
{
	int candle_rising_nr;
	uint32_t max_up_diff = 0;
	int is_rising = 1;
	const struct date_price *lowest_date = NULL;

	for (candle_rising_nr = 0; candle_rising_nr < max_sr_candle_nr && idx < price_history->date_cnt; idx++, candle_rising_nr++) {
		const struct date_price *prev = &price_history->dateprice[idx];

		if (prev->close > price2check->close) {
			if (candle_rising_nr < min_sr_candle_nr - 1)
				is_rising = 0;
			break;
		}

		if (prev->low < price2check->high && price2check->high - prev->low > max_up_diff) {
			max_up_diff = price2check->high - prev->low;
			lowest_date = prev;
		}
	}

	if (!is_rising || (max_up_diff * 1000 / get_2ndlow(lowest_date) < bo_sr_height_margin))
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

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if (yesterday == NULL) {
			yesterday = prev;
			if (!date_is_downtrend(price_history, i, price2check))
				break;
		}

		if (prev->low < lowest_date->low)
			lowest_date = prev;

		int datecnt = i + 1;
		uint32_t prev_2ndhigh = get_2ndhigh(prev);
		uint32_t prev_2ndlow = get_2ndlow(prev);
		uint32_t price2check_2ndlow = get_2ndlow(price2check);

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

static void check_breakout(const struct stock_price *price_history, const struct date_price *price2check, struct stock_support *sspt)
{
	const struct date_price *yesterday = NULL;
	int i;

	sspt->date_nr = 0;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) <= 0)
			continue;

		if (yesterday == NULL) {
			yesterday = prev;
			if (!date_is_uptrend(price_history, i, price2check))
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
				continue;
			}
		}
	}
}

static int get_stock_price2check(const char *symbol, const char *date,
				const struct stock_price *price_history,
				struct date_price *price2check)
{
	if (strcmp(date, "realtime") == 0) { /* get real time price */
		if (fetch_realtime_price(symbol, price2check) < 0)
			return -1;
	}
	else if (!date || !date[0]) {
		memcpy(price2check, &price_history->dateprice[0], sizeof(*price2check));
	}
	else {
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
		anna_error("%s: get_stock_price2check(%s)\n", symbol, date);
		return -1;
	}

	return 0;
}

static void symbol_check_support(const char *symbol, const struct stock_price *price_history,
				 const struct date_price *price2check)
{
	struct stock_support sspt = { };
	int i;

	check_support(price_history, price2check, &sspt);

	if (!sspt.date_nr)
		return;

	anna_info("%s%s%s: date=%s is supported by %d dates:",
		  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET, price2check->date, sspt.date_nr);

	for (i = 0; i < sspt.date_nr; i++)
		anna_info(" %s(%c)", sspt.date[i], is_support(sspt.sr_flag[i]) ? 's' : is_resist(sspt.sr_flag[i]) ? 'r' : '?');

	anna_info(". %s<sector=%s>%s\n", ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);
}

static void symbol_check_doublebottom(const char *symbol, const struct stock_price *price_history,
					const struct date_price *price2check)
{
	struct stock_support sspt = { };
	int i;

	check_support(price_history, price2check, &sspt);

	if (!sspt.date_nr)
		return;

	for (i = 0; i < sspt.date_nr; i++) {
		if (sspt.is_doublebottom[i]) {
			anna_info("%s%s%s: date=%s is double bottom with dates=%s. %s<sector=%s>%s\n",
				ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET, price2check->date, sspt.date[i],
				ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);
			break;
		}
	}
}

static void symbol_check_pullback(const char *symbol, const struct stock_price *price_history,
				  const struct date_price *price2check)
{
	int i, j;

	for (i = 0; i < price_history->date_cnt; i++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (strcmp(price2check->date, prev->date) > 0)
			break;
	}

	if (price_history->date_cnt - i < 5) /* need to back trace at least 5 days */
		return;

	for (j = 0; j < 20 && i < price_history->date_cnt; i++, j++) {
		const struct date_price *prev = &price_history->dateprice[i];

		if (!(prev->sr_flag & SR_F_BIGUPDAY))
			continue;

		uint32_t prev_2ndhigh = get_2ndhigh(prev);
		uint32_t prev_2ndlow = get_2ndlow(prev);
		uint32_t price2check_2ndlow = get_2ndlow(price2check);

		if (sr_hit(price2check->low, prev->low) || sr_hit(price2check_2ndlow, prev->low)
		    || sr_hit(price2check->low, prev_2ndlow) || sr_hit(price2check_2ndlow, prev_2ndlow))
		{
			anna_info("%s%s%s: date=%s hit bigupdate=%s at support. %s<sector=%s>%s\n",
				  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
				  price2check->date, prev->date,
				  ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);
			break;
		}
		else if (sr_hit(price2check->low, prev->high) || sr_hit(price2check_2ndlow, prev->high)
			 || sr_hit(price2check->low, prev_2ndhigh) || sr_hit(price2check_2ndlow, prev_2ndhigh))
		{
			anna_info("%s%s%s: date=%s hit bigupdate=%s at resist. %s<sector=%s>%s\n",
				  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
				  price2check->date, prev->date,
				  ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);
			break;
		}
	}
}

static void symbol_check_breakout(const char *symbol, const struct stock_price *price_history,
				 const struct date_price *price2check)
{
	struct stock_support sspt = { };
	int i;

	check_breakout(price_history, price2check, &sspt);

	if (!sspt.date_nr)
		return;

	anna_info("%s%s%s: date=%s breakout with %d dates:",
		  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET, price2check->date, sspt.date_nr);

	for (i = 0; i < sspt.date_nr; i++)
		anna_info(" %s(%c)", sspt.date[i], is_support(sspt.sr_flag[i]) ? 's' : is_resist(sspt.sr_flag[i]) ? 'r' : '?');

	anna_info(". %s<sector=%s>%s\n", ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);
}

static void get_week_price(const struct stock_price *price_history, int *idx, struct date_price *week_price)
{
	int j;

	memset(week_price, 0, sizeof(*week_price));
	week_price->low = (uint32_t)-1;

	for (j = 0; j < 5 && *idx < price_history->date_cnt; (*idx)++, j++) {
		const struct date_price *cur = &price_history->dateprice[*idx];

		if (j == 0)
			week_price->close = cur->close;

		if (cur->high > week_price->high)
			week_price->high = cur->high;

		if (cur->low < week_price->low)
			week_price->low = cur->low;

		if (cur->wday == 1) {
			week_price->open = cur->open;
			(*idx) += 1;
			break;
		}
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
	    && w2_price.close > w1_price.close)
	{
		anna_info("%s%s%s: has continuous 2 weeks uptrend. %s<sector=%s>%s\n",
			  ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
			  ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);
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

		if (price2check->volume && prev->vma[VMA_10d]
		    && (price2check->low <= prev->low || price2check->close <= prev->close || prev->low <= price2check->close)
		    && price2check->volume * 100 / prev->vma[VMA_10d] <= 75)
		{
			anna_info("\n%s%s%s: date=%s has low volume, %u/%u. %s<sector=%s>%s\n\n",
				ANSI_COLOR_YELLOW, symbol, ANSI_COLOR_RESET,
				price2check->date, price2check->volume, prev->vma[VMA_10d],
				ANSI_COLOR_YELLOW, price_history->sector, ANSI_COLOR_RESET);
		}

		break;
	}
}

static int call_check_func(const char *symbol, const char *date, const char *fname,
			    void (*check_func)(const char *, const struct stock_price *, const struct date_price *))
{
	struct stock_price price_history;
	struct date_price price2check;

	if (get_symbol_price_for_check(symbol, date, fname, &price_history, &price2check) < 0) {
		anna_error("get_symbol_price_for_check(symbol=%s,date=%s,fname=%s) failed\n", symbol, date, fname);
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
}

void stock_price_check_support(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_support);
}

void stock_price_check_doublebottom(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_doublebottom);
}

void stock_price_check_pullback(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_pullback);
}

void stock_price_check_breakout(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_breakout);
}

void stock_price_check_weekup(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_weekup);
}

void stock_price_check_low_volume(const char *group, const char *date, int symbols_nr, const char **symbols)
{
	stock_price_check(group, date, symbols_nr, symbols, symbol_check_low_volume);
}
