#ifndef __STOCK_PRICE_H__
#define __STOCK_PRICE_H__

#include <stdint.h>

enum
{
	CANDLE_COLOR_DOJI,
	CANDLE_COLOR_GREEN,
	CANDLE_COLOR_RED,

	CANDLE_COLOR_NR
};

enum
{
	CANDLE_TREND_DOJI,
	CANDLE_TREND_BULL,
	CANDLE_TREND_BEAR,

	CANDLE_TREND_NR
};

enum
{
	SMA_10d,
	SMA_20d,
	SMA_30d,
	SMA_50d,
	SMA_60d,
	SMA_100d,
	SMA_120d,
	SMA_200d,

	SMA_NR
};

enum
{
	VMA_10d,
	VMA_20d,
	VMA_60d,

	VMA_NR
};

#define SR_F_SUPPORT_LOW	(1<<0)
#define SR_F_SUPPORT_2ndLOW	(1<<1)
#define SR_F_RESIST_HIGH	(1<<2)
#define SR_F_RESIST_2ndHIGH	(1<<3)

struct date_price
{
	char      date[12];
	uint64_t  open, high, low, close;
	uint64_t  volume;
	uint64_t  sma[SMA_NR];
	uint64_t  vma[VMA_NR];
	uint8_t   candle_color;
	uint8_t   candle_trend;
	uint16_t  sr_flag; /* support/resist flag: SR_F_xxx */
	uint64_t  height_low_spt, height_2ndlow_spt, height_high_rst, height_2ndhigh_rst;
};

struct stock_price
{
	int date_cnt;

#define DATE_PRICE_SZ_MAX   1024
	struct date_price dateprice[DATE_PRICE_SZ_MAX];
};

int get_stock_price_from_file(const char *fname, int today_only, struct stock_price *price);

#endif /* __STOCK_PRICE_H__ */
