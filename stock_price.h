#ifndef __STOCK_PRICE_H__
#define __STOCK_PRICE_H__

#include <stdint.h>

enum
{
	BAR_COLOR_DOJI,
	BAR_COLOR_GREEN,
	BAR_COLOR_RED,

	BAR_COLOR_NR
};

enum
{
	BAR_TREND_DOJI,
	BAR_TREND_BULL,
	BAR_TREND_BEAR,

	BAR_TREND_NR
};

struct stock_price
{
	char      date[12];
	uint64_t  open, high, low, close;
	uint64_t  volume;
	uint64_t  sma_10d, sma_20d, sma_30d, sma_50d, sma_60d, sma_100d, sma_120d, sma_200d;
	uint64_t  vma_10d, vma_20d, vma_60d; /* average volume */
	uint8_t   bar_color;
	uint8_t   bar_trend;
};

int get_stock_price_from_file(const char *fname, int today_only, struct stock_price *prices, int *price_cnt);

#endif /* __STOCK_PRICE_H__ */
