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
#define is_support(sr_flag) (sr_flag & (SR_F_SUPPORT_LOW | SR_F_SUPPORT_2ndLOW))
#define is_resist(sr_flag) (sr_flag &(SR_F_RESIST_HIGH | SR_F_RESIST_2ndHIGH))

struct date_price
{
#define STOCK_DATE_SZ 12
	char      date[STOCK_DATE_SZ];
	uint64_t  open, high, low, close;
	uint64_t  volume;
	uint64_t  sma[SMA_NR];
	uint64_t  vma[VMA_NR];
	uint8_t   candle_color;
	uint8_t   candle_trend;
	uint16_t  sr_flag; /* support/resist flag: SR_F_xxx */
	uint64_t  height_low_spt, height_2ndlow_spt, height_high_rst, height_2ndhigh_rst;

	/* the following fields are NOT stored in db */
	int8_t    updated;
};

struct stock_price
{
	int date_cnt;

#define DATE_PRICE_SZ_MAX   1024
	struct date_price dateprice[DATE_PRICE_SZ_MAX];
};

int stock_price_get_from_file(const char *fname, int today_only, struct stock_price *price);
void stock_price_check_support(const char *date, const char **symbols, int symbols_nr);
void stock_price_update(const char *symbol);

#endif /* __STOCK_PRICE_H__ */
