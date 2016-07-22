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

int fetch_symbols_price(int realtime, const char *group, const char *fname, int symbols_nr, const char **symbols);

#endif /* __FECTCH_PRICE_H__ */
