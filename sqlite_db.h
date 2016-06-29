#ifndef __SQLITE_DB_H__
#define __SQLITE_DB_H__

int db_open(void);
int db_close(void);
int db_symbol_exist(const char *symbol);
int db_insert_stock_info(const char *symbol, const char *name, const char *exchange,
			const char *sector, const char *industry, const char *country);


#endif /* __SQLITE_DB_H__ */
