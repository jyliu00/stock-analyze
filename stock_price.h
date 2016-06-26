#ifndef __STOCK_PRICE_H__
#define __STOCK_PRICE_H__

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
	char date[12];
	int  open, high, low, close;
	int  volume;
	int  sma_10, sma_20, sma_30, sma_50, sma_60, sma_100, sma_120, sma_200;
	int  volume_10, volume_20, volume_60;
	int  bar_color;
	int  bar_trend;
};

#endif /* __STOCK_PRICE_H__ */
