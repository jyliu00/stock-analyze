#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

enum
{
	STOCK_BAR_T_BULL,
	STOCK_BAR_T_BEAR,
	STOCK_BAR_T_DOJI,

	STOCK_BAR_T_NR
};

struct stock_price
{
	int64_t  cent; /* use cent so that the price can be represented in int format */
};

#define cmp_stock_price(p1, p2) ((p1)->cent - (p2)->cent)

#define CENT2DOLLAR(cent) (cent)/100, (cent)%100

static const char *bartype2str(int bar_type)
{
	if (bar_type < STOCK_BAR_T_BULL || bar_type >= STOCK_BAR_T_NR)
		return "unknown";
	switch (bar_type) {
	case STOCK_BAR_T_BULL: return "bull";
	case STOCK_BAR_T_BEAR: return "bear";
	case STOCK_BAR_T_DOJI: return "doji";
	default: return "unknown";
	}
}

static void get_stock_price_diff(const struct stock_price *p1,
				 const struct stock_price *p2,
				 struct stock_price *price_diff)
{
	price_diff->cent = p1->cent - p2->cent;
}

static void analyze_money_flow(struct stock_price *open, struct stock_price *close,
				struct stock_price *high, struct stock_price *low,
				int volume)
{
	struct stock_price down_price_range, up_price_range, total_price_range;
	struct stock_price top_tail_range, bottom_tail_range;
	struct stock_price open_close_diff;
	struct stock_price typical_price;
	int64_t money_in, money_out, money_diff;
	int bar_type, bar_trend;
	int64_t money_total;

	/* calculate typical price */
	typical_price.cent = (high->cent + low->cent + close->cent) / 3;

	get_stock_price_diff(open, close, &open_close_diff);

	/* set bar trend */
	if (open_close_diff.cent < 0)
		bar_trend = STOCK_BAR_T_BULL;
	else if (open_close_diff.cent > 0)
		bar_trend = STOCK_BAR_T_BEAR;
	else
		bar_trend = STOCK_BAR_T_DOJI;

	/* set bar type */
	if (open_close_diff.cent < 0)
		bar_type = STOCK_BAR_T_BULL;
	else if (open_close_diff.cent > 0)
		bar_type = STOCK_BAR_T_BEAR;
	else
		bar_type = STOCK_BAR_T_DOJI;

	if (open_close_diff.cent < 0)
		money_diff = -open_close_diff.cent;

	if (money_diff * 100 / open->cent < 1)
		bar_type = STOCK_BAR_T_DOJI;

	/* get price ranges */
	get_stock_price_diff(high, close, &down_price_range);
	get_stock_price_diff(close, low, &up_price_range);
	get_stock_price_diff(high, low, &total_price_range);

	/* get top/bottom tail percentage */
	if (open_close_diff.cent < 0) {
		get_stock_price_diff(high, close, &top_tail_range);
		get_stock_price_diff(open, low, &bottom_tail_range);
	}
	else {
		get_stock_price_diff(high, open, &top_tail_range);
		get_stock_price_diff(close, low, &bottom_tail_range);
	}

	/* calculate money flow */
	money_total = typical_price.cent * volume;
	money_in = (money_total * up_price_range.cent) / total_price_range.cent;
	money_out = (money_total * down_price_range.cent) / total_price_range.cent;
	money_diff = money_in - money_out;

	printf("Money Flow ====>\n");
	printf("    Open: $%zd.%02zd, Close: $%zd.%02zd, High: $%zd.%02zd, Low: $%zd.%02zd\n",
		CENT2DOLLAR(open->cent), CENT2DOLLAR(close->cent), CENT2DOLLAR(high->cent), CENT2DOLLAR(low->cent));
	printf("    Bar Type: %s, Bar Trend: %s\n", bartype2str(bar_type), bartype2str(bar_trend));
	printf("    Top Tail: %zd.%zd%%, Bottom Tail: %zd.%zd%%\n",
		top_tail_range.cent * 100 / total_price_range.cent, top_tail_range.cent * 100 % total_price_range.cent,
		bottom_tail_range.cent * 100 / total_price_range.cent, bottom_tail_range.cent * 100 % total_price_range.cent);
	printf("    Typical Price:  $%zd.%02zd\n", CENT2DOLLAR(typical_price.cent));
	printf("    Money Total:    $%zd.%02zd\n", CENT2DOLLAR(money_total));
	printf("    Money In:       $%zd.%02zd\n", CENT2DOLLAR(money_in));
	printf("    Money Out:      $%zd.%02zd\n", CENT2DOLLAR(money_out));
	if (money_diff >= 0)
		printf("    Money Flow:    +$%zd.%02zd,", CENT2DOLLAR(money_diff));
	else
		printf("    Money Flow:    -$%zd.%02zd,", CENT2DOLLAR(-money_diff));

	printf(" In=%zd.%02zd%%, Out=%zd.%02zd%%\n",
		(money_in * 100) / money_total, (money_in * 100) % money_total,
		(money_out * 100) / money_total, (money_out * 100) % money_total);
}

static int parse_price(char *str, struct stock_price *price)
{
	price->cent = 0;

	char *dot = strchr(str, '.');
	if (dot)
		*dot = 0;

	price->cent = atoi(str) * 100;

	if (dot) {
		price->cent += atoi(dot + 1);
		*dot = '.';
	}

	return 0;
}

int _main(int argc, const char **argv)
{
	struct stock_price open, close, high, low;
	int volume;
	char price_str[128];

	if (argc != 6) {
		printf("\nUsage: anna open close high low volume\n\n");
		return 0;
	}

	strcpy(price_str, argv[1]);
	parse_price(price_str, &open);

	strcpy(price_str, argv[2]);
	parse_price(price_str, &close);

	strcpy(price_str, argv[3]);
	parse_price(price_str, &high);

	strcpy(price_str, argv[4]);
	parse_price(price_str, &low);

	volume = atoi(argv[5]);

	analyze_money_flow(&open, &close, &high, &low, volume);

	return 0;
}
