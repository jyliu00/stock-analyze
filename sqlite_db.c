#include "sqlite_db.h"

#include "util.h"

#include "sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_STOCK_INFO "stock_info"

#define sqlite_error(sqlite_rt, fmt, args...) \
	fprintf(stderr, "[%s:%s:%d] sqlite_rt=%d(%s), " fmt, __FILE__, __FUNCTION__, __LINE__, sqlite_rt, sqlite3_errstr(sqlite_rt), ##args)

#define check_sqlite_rt(sqlite_rt, fmt, args...) \
	do { \
		if (sqlite_rt != SQLITE_OK) { \
			sqlite_error(sqlite_rt, fmt, ##args); \
			return -1; \
		} \
	} while (0)

static sqlite3 *sqlite_db;

static const char *get_db_name(void)
{
	return "usa_stock.db";
}

static int get_count(void *cb, int column_nr, char **column_text, char **column_name)
{
	int *count = cb;

	if (column_nr == 1)
		*count = atoi(column_text[0]);

	return 0;
}

static int check_exist(const char *tablename, const char *where_str)
{
	char stmt_str[1024];
	int sqlite_rt;
	int count = 0;

	snprintf(stmt_str, sizeof(stmt_str), "SELECT COUNT(*) FROM %s WHERE %s;", tablename, where_str);

	sqlite_rt = sqlite3_exec(sqlite_db, stmt_str, get_count, &count, NULL);
	if (sqlite_rt != SQLITE_OK) {
		sqlite_error(sqlite_rt, "run '%s' failed\n", stmt_str);
		return 0;
	}

	return !!count;
}

static int table_exist(const char *tablename)
{
	char where_str[1024];

	snprintf(where_str, sizeof(where_str), "type='table' and name='%s'", tablename);

	return check_exist("sqlite_master", where_str);
}

static int insert_into_table(const char *tablename, const char *columns_name, const char *values)
{
	char stmt_str[1024];

	if (columns_name)
		snprintf(stmt_str, sizeof(stmt_str), "INSERT INTO %s (%s) VALUES (%s);", tablename, columns_name, values);
	else
		snprintf(stmt_str, sizeof(stmt_str), "INSERT INTO %s VALUES (%s);", tablename, values);

	int sqlite_rt = sqlite3_exec(sqlite_db, stmt_str, NULL, NULL, NULL);
	check_sqlite_rt(sqlite_rt, "run '%s' failed\n", stmt_str);

	return 0;
}

static int delete_from_table(const char *tablename, const char *where_cond)
{
	char stmt_str[1024];

	if (where_cond && where_cond[0])
		snprintf(stmt_str, sizeof(stmt_str), "DELETE FROM %s WHERE %s;", tablename, where_cond);
	else
		snprintf(stmt_str, sizeof(stmt_str), "DELETE FROM %s;", tablename);

	int sqlite_rt = sqlite3_exec(sqlite_db, stmt_str, NULL, NULL, NULL);
	check_sqlite_rt(sqlite_rt, "run '%s' failed\n", stmt_str);

	return 0;
}

static int drop_table(const char *tablename)
{
	char stmt_str[1024];

	snprintf(stmt_str, sizeof(stmt_str), "DROP TABLE %s;", tablename);

	int sqlite_rt = sqlite3_exec(sqlite_db, stmt_str, NULL, NULL, NULL);
	check_sqlite_rt(sqlite_rt, "run '%s' failed\n", stmt_str);

	return 0;
}

static int create_table(const char *tablename, const char *columns)
{
	char stmt_str[1024];
	int sqlite_rt;

	snprintf(stmt_str, sizeof(stmt_str), "CREATE TABLE %s (%s) WITHOUT ROWID;", tablename, columns);

	sqlite_rt = sqlite3_exec(sqlite_db, stmt_str, NULL, NULL, NULL);
	check_sqlite_rt(sqlite_rt, "run '%s' failed\n", stmt_str);

	return 0;
}

static int create_symbol_table(const char *symbol)
{
	char column_def[1024];

	snprintf(column_def, sizeof(column_def),
		 "date CHAR(12) PRIMARY KEY"
		 ", open INTEGER"
		 ", high INTEGER"
		 ", low  INTEGER"
		 ", close INTEGER"
		 ", volume INTEGER"
		 ", sma_10d INTEGER"
		 ", sma_20d INTEGER"
		 ", sma_30d INTEGER"
		 ", sma_50d INTEGER"
		 ", sma_60d INTEGER"
		 ", sma_100d INTEGER"
		 ", sma_120d INTEGER"
		 ", sma_200d INTEGER"
		 ", vma_10d INTEGER"
		 ", vma_20d INTEGER"
		 ", vma_60d INTEGER"
		 ", candle_color TINYINT"
		 ", candle_trend TINYINT"
		 ", sr_flag TINYINT"
		 ", height_low_spt INTEGER"
		 ", height_2ndlow_spt INTEGER"
		 ", height_high_rst INTEGER"
		 ", height_2ndhigh_rst INTEGER"
		);

	return create_table(symbol, column_def);
}

static int __init_db( )
{
	char column_def[1024];

	if (table_exist(TABLE_STOCK_INFO) > 0)
		return 0;

	snprintf(column_def, sizeof(column_def),
		 "symbol CHAR(10) PRIMARY KEY"
		 ", name CHAR(64) NOT NULL"
		 ", exchange CHAR(8)"
		 ", sector TEXT"
		 ", industry TEXT"
		 ", country CHAR(32)"
		);

	if (create_table(TABLE_STOCK_INFO, column_def) < 0)
		return -1;

	return 0;
}

static int __open_db( )
{
	const char *db_name = get_db_name( );
	int sqlite_rt;

	if (sqlite_db) {
		fprintf(stderr, "[%s:%s:%d] sqlite_db already opened\n",
			__FILE__, __FUNCTION__, __LINE__);
		return -1;
	}

	sqlite_rt = sqlite3_open_v2(db_name, &sqlite_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	check_sqlite_rt(sqlite_rt, "sqlite3_open_v2(%s) failed\n", db_name);

	return 0;
}

int db_open(void)
{
	if (__open_db( ) < 0)
		return -1;

	if (__init_db( ) < 0)
		return -1;

	return 0;
}

int db_close(void)
{
	if (!sqlite_db)
		return 0;

	sqlite3_close_v2(sqlite_db);
	sqlite_db = NULL;

	return 0;
}

int db_symbol_exist(const char *symbol)
{
	char where_str[1024];

	if (!symbol || !symbol[0])
		return 0;

	snprintf(where_str, sizeof(where_str), "symbol='%s'", symbol);

	return check_exist(TABLE_STOCK_INFO, where_str);
}

int db_insert_stock_info(const char *symbol, const char *name, const char *exchange,
			const char *sector, const char *industry, const char *country)
{
	char values_str[1024];

	snprintf(values_str, sizeof(values_str), "'%s', '%s', '%s', '%s', '%s', '%s'", symbol, name, exchange, sector, industry, country);

	return insert_into_table(TABLE_STOCK_INFO, NULL, values_str);
}

int db_insert_stock_price(const char *symbol, const struct stock_price *price)
{
	char values_str[1024];
	int i;

	if (table_exist(symbol) == 0) {
		if (create_symbol_table(symbol) < 0)
			return -1;
	}

	for (i = 0; i < price->date_cnt; i++) {
		const struct date_price *p = &price->dateprice[i];

		/* date, open, high, low, close, volume,
		   sma_10/20/30/50/60/100/120/200d, vma_10/20/60d,
		   candle_color, candle_trend, sr_flag,
		   height_low_spt, height_2ndlow_spt, height_high_rst, height_2ndhigh_rst
		*/
		snprintf(values_str, sizeof(values_str),
			 "'%s', %zu, %zu, %zu, %zu, %zu" /* date, open, high, low, close, volume */
			 ", %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu" /* sma_10/20/30/50/60/100/120/200d */
			 ", %zu, %zu, %zu" /* vma_10/20/60d */
			 ", %d, %d, %d" /* candle_color, candle_trend, sr_flag */
			 ", %zu, %zu, %zu, %zu", /* height_low_spt, height_2ndlow_spt, height_high_rst, height_2ndhigh_rst */
			 p->date, p->open, p->high, p->low, p->close, p->volume,
			 p->sma[SMA_10d], p->sma[SMA_20d], p->sma[SMA_30d], p->sma[SMA_50d],
			 p->sma[SMA_60d], p->sma[SMA_100d], p->sma[SMA_120d], p->sma[SMA_200d],
			 p->vma[VMA_10d], p->vma[VMA_20d], p->vma[VMA_60d],
			 p->candle_color, p->candle_trend, p->sr_flag,
			 p->height_low_spt, p->height_2ndlow_spt, p->height_high_rst, p->height_2ndhigh_rst);

		if (insert_into_table(symbol, NULL, values_str) < 0) {
			anna_error("insert_into_table(%s, date=%s) failed\n", symbol, p->date);
		}
	}

	return 0;
}

int db_delete_symbol(const char *symbol)
{
	char where_cond[256];

	if (!db_symbol_exist(symbol)) {
		anna_error("symbol '%s' doesn't exist\n", symbol);
		return -1;
	}

	snprintf(where_cond, sizeof(where_cond), "symbol='%s'", symbol);

	if (delete_from_table(TABLE_STOCK_INFO, where_cond) < 0)
		return -1;

	if (drop_table(symbol) < 0)
		return -1;

	return 0;
}

struct symbol_array
{
	int *symbols_nr;
	char **symbols;
};

static int get_symbols(void *cb, int column_nr, char **column_text, char **column_name)
{
	struct symbol_array *a = cb;
	int idx = *a->symbols_nr;

	strcpy(a->symbols[idx], column_text[0]);

	(*a->symbols_nr) += 1;

	return 0;
}

int db_get_all_symbols(char **symbols, int *symbols_nr)
{
	char stmt_str[1024];
	struct symbol_array symbol_arr;
	int sqlite_rt;

	*symbols_nr = 0;
	symbol_arr.symbols_nr = symbols_nr;
	symbol_arr.symbols = symbols;

	snprintf(stmt_str, sizeof(stmt_str), "SELECT symbol FROM %s;", TABLE_STOCK_INFO);

	sqlite_rt = sqlite3_exec(sqlite_db, stmt_str, get_symbols, &symbol_arr, NULL);
	check_sqlite_rt(sqlite_rt, "run '%s' failed\n", stmt_str);

	return 0;
}

static int get_price_history(void *cb, int column_nr, char **column_text, char **column_name)
{
	struct stock_price *prices = cb;
	struct date_price *cur = &prices->dateprice[prices->date_cnt];

	strlcpy(cur->date, column_text[0], sizeof(cur->date));
	cur->open = atoi(column_text[1]);
	cur->high = atoi(column_text[2]);
	cur->low = atoi(column_text[3]);
	cur->close = atoi(column_text[4]);
	cur->volume = atoi(column_text[5]);
	cur->sr_flag = atoi(column_text[6]);
	cur->height_low_spt = atoi(column_text[7]);
	cur->height_2ndlow_spt = atoi(column_text[8]);
	cur->height_high_rst = atoi(column_text[9]);
	cur->height_2ndhigh_rst = atoi(column_text[10]);

	prices->date_cnt += 1;

	return 0;
}

int db_get_symbol_price_history(const char *symbol, const char *date, int date_include, struct stock_price *price_history)
{
	char stmt_str[1024];
	const char *columns = "date, open, high, low, close, volume, sr_flag, height_low_spt, height_2ndlow_spt, height_high_rst, height_2ndhigh_rst";
	int sqlite_rt;

	if (date)
		snprintf(stmt_str, sizeof(stmt_str), "SELECT %s FROM %s WHERE date %s '%s';", columns, symbol, date_include ? "<=" : "<", date);
	else
		snprintf(stmt_str, sizeof(stmt_str), "SELECT %s FROM %s;", columns, symbol);

	price_history->date_cnt = 0;

	sqlite_rt = sqlite3_exec(sqlite_db, stmt_str, get_price_history, price_history, NULL);
	check_sqlite_rt(sqlite_rt, "run '%s' failed\n", stmt_str);

	return 0;
}

static int get_date_price(void *cb, int column_nr, char **column_text, char **column_name)
{
	struct date_price *price = cb;

	strlcpy(price->date, column_text[0], sizeof(price->date));
	price->open = atoi(column_text[1]);
	price->high = atoi(column_text[2]);
	price->low = atoi(column_text[3]);
	price->close = atoi(column_text[4]);
	price->volume = atoi(column_text[5]);

	return 0;
}

int db_get_symbol_price_by_date(const char *symbol, const char *date, struct date_price *price)
{
	char stmt_str[1024];
	const char *columns = "date, open, high, low, close, volume";
	int sqlite_rt;

	snprintf(stmt_str, sizeof(stmt_str), "SELECT %s FROM %s WHERE date='%s';", columns, symbol, date);

	sqlite_rt = sqlite3_exec(sqlite_db, stmt_str, get_date_price, price, NULL);
	check_sqlite_rt(sqlite_rt, "run '%s' failed\n", stmt_str);

	return 0;
}
