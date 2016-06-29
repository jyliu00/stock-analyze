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
	uint64_t  sma_10, sma_20, sma_30, sma_50, sma_60, sma_100, sma_120, sma_200;
	uint64_t  volume_10, volume_20, volume_60;
	uint8_t   bar_color;
	uint8_t   bar_trend;
};

int get_stock_price_from_file(const char *fname, int today_only, struct stock_price *prices, int *price_cnt);

#endif /* __STOCK_PRICE_H__ */
