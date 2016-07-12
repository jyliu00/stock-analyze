#ifndef __FETCH_PRICE_H__
#define __FETCH_PRICE_H__

enum
{
	FETCH_ACTION_ADD,
	FETCH_ACTION_DEL,
	FETCH_ACTION_UPDATE,

	FETCH_ACTION_NR
};

struct date_price;

void fetch_symbols_price(const char *country, const char *fname, int symbols_nr, const char **symbols);
int fetch_realtime_price(const char *symbol, struct date_price *realtime_price);

#endif /* __FECTCH_PRICE_H__ */
