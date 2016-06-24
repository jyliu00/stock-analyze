#include "sqlite3.h"

#include <stdio.h>
#include <stdlib.h>

#define STOCK_DB_NAME "stocks.db"
#define TABLE_STOCK_METAINFO "stock_metainfo"

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
	check_sqlite_rt(sqlite_rt, "run '%s' failed\n", stmt_str);

	return count;
}

static int table_exist(const char *tablename)
{
	char where_str[1024];

	snprintf(where_str, sizeof(where_str), "type='table' and name='%s'", tablename);

	return check_exist("sqlite_master", where_str);
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

static int __init_db( )
{
	char column_def[1024];

	if (table_exist(TABLE_STOCK_METAINFO) > 0)
		return 0;

	snprintf(column_def, sizeof(column_def),
		 "symbol CHAR(10) PRIMARY KEY, "
		 "name CHAR(64) NOT NULL, "
		 "exchange CHAR(8), "
		 "SMA_10 CHAR(10), "
		 "SMA_20 CHAR(10), "
		 "SMA_50 CHAR(10), "
		 "SMA_60 CHAR(10), "
		 "SMA_100 CHAR(10), "
		 "SMA_120 CHAR(10), "
		 "SMA_200 CHAR(10), "
		 "SMA_250 CHAR(10)"
		);

	if (create_table(TABLE_STOCK_METAINFO, column_def) < 0)
		return -1;

	return 0;
}

static int __open_db( )
{
	int sqlite_rt;

	if (sqlite_db) {
		fprintf(stderr, "[%s:%s:%d] sqlite_db already opened\n",
			__FILE__, __FUNCTION__, __LINE__);
		return -1;
	}

	sqlite_rt = sqlite3_open_v2(STOCK_DB_NAME, &sqlite_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	check_sqlite_rt(sqlite_rt, "sqlite3_open_v2(%s) failed\n", STOCK_DB_NAME);

	return 0;
}

int sqlite_db_init(void)
{
	if (__open_db( ) < 0)
		return -1;

	if (__init_db( ) < 0)
		return -1;

	return 0;
}

int stock_symbol_exist(const char *symbol)
{
	char where_str[1024];

	snprintf(where_str, sizeof(where_str), "name='%s'", symbol);

	return check_exist(TABLE_STOCK_METAINFO, where_str);
}
