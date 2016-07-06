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

int fetch_price(int fetch_action, int symbol_nr, const char **symbols);
int fetch_today_price(const char *symbol, struct date_price *cur_price);

#endif /* __FECTCH_PRICE_H__ */
