#ifndef __STOCK_PRICE_H__
#define __STOCK_PRICE_H__

#include <stdint.h>
#include <stdio.h>

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
#define SR_F_BIGUPDAY		(1<<4)
#define is_support(sr_flag) (sr_flag & (SR_F_SUPPORT_LOW | SR_F_SUPPORT_2ndLOW))
#define is_resist(sr_flag) (sr_flag &(SR_F_RESIST_HIGH | SR_F_RESIST_2ndHIGH))

struct date_price
{
#define STOCK_DATE_SZ 12
	char      date[STOCK_DATE_SZ];
	uint8_t   wday;
	uint32_t  open, high, low, close;
	uint32_t  volume;
	uint32_t  sma[SMA_NR];
	uint32_t  vma[VMA_NR];
	uint8_t   candle_color;
	uint8_t   candle_trend;
	uint16_t  sr_flag; /* support/resist flag: SR_F_xxx */
	uint32_t  height_low_spt, height_2ndlow_spt, height_high_rst, height_2ndhigh_rst;
};

struct stock_price
{
	char sector[48];

#define DATE_PRICE_SZ_MAX   1024
	int date_cnt;
	struct date_price dateprice[DATE_PRICE_SZ_MAX];
};

int stock_price_realtime_from_file(const char *output_fname, struct date_price *price);
int stock_price_from_file(const char *fname, struct stock_price *price);
int stock_price_to_file(const char *group, const char *sector, const char *symbol, const struct stock_price *price);
void fprintf_date_price(FILE *fp, const struct date_price *p);
void stock_price_check_support(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_sma(const char *group, const char *date, int sma_idx, int symbols_nr, const char **symbols);
void stock_price_check_doublebottom(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_doublebottom_up(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_pullback(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_breakout(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_52w_low_up(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_weekup(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_week_reverse(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_low_volume(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_low_volume_up(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_volume_up(const char *group, const char *date, int symbols_nr, const char **symbols);
void stock_price_check_change(const char *group, const char *date, int symbols_nr, const char **symbols);

#endif /* __STOCK_PRICE_H__ */
