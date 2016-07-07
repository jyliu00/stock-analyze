#ifndef __SQLITE_DB_H__
#define __SQLITE_DB_H__

#include "stock_price.h"

int db_open(void);
int db_close(void);
int db_symbol_exist(const char *symbol);
int db_insert_symbol_info(const char *symbol, const char *name, const char *exchange,
			const char *sector, const char *industry, const char *country);
int db_insert_symbol_price(const char *symbol, const struct stock_price *price, int start_idx);
int db_delete_symbol(const char *symbol);
int db_get_all_symbols(char **symbols, int *symbols_nr);
int db_get_symbol_price_history(const char *symbol, const char *date, int date_include, struct stock_price *price_history);
int db_get_symbol_price_by_date(const char *symbol, const char *date, struct date_price *price);
int db_delete_symbol_price_by_date(const char *symbol, const char *date);

#endif /* __SQLITE_DB_H__ */
