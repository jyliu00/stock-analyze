#include "sqlite_db.h"
#include "fetch_price.h"

#include <stdio.h>
#include <string.h>

static void print_usage(void)
{
	printf("Usage: anna\n");
	printf("       anna {-add | -delete} {symbol-1 symbol-2 ...}\n");
	printf("       anna -update [symbol-1 symbol-2 ...]\n");
	printf("       anna -check-support [-date yyyy-mm-dd] [symbol-1 symbol-2 ...]\n");
	printf("       anna -add-file filename eft-index-name\n");
	printf("       anna -help\n");
}

int main(int argc, const char **argv)
{
	if (db_open( ) < 0)
		return -1;

	if (argc < 2) {
		goto usage;
	}
	else if (!strcmp(argv[1], "-add")) {
		if (argc <= 2)
			goto usage;

		fetch_price(FETCH_ACTION_ADD, argc - 2, &argv[2]);
	}
	else if (!strcmp(argv[1], "-add-file")) {
		if (argc < 4)
			goto usage;

		fetch_price_by_file(argv[2], argv[3]);
	}
	else if (!strcmp(argv[1], "-delete")) {
		if (argc <= 2)
			goto usage;

		fetch_price(FETCH_ACTION_DEL, argc - 2, &argv[2]);
	}
	else if (!strcmp(argv[1], "-update")) {
		fetch_price(FETCH_ACTION_UPDATE, argc - 2, &argv[2]);
	}
	else if (!strcmp(argv[1], "-check-support")) {
		const char *date = NULL;
		const char **symbols = NULL;
		int symbols_nr = 0;

		if (argc < 3) {
		}
		else if (!strcmp(argv[2], "-date")) {
			if (argc < 4)
				goto usage;

			date = argv[3];

			if (argc > 4) {
				symbols = &argv[4];
				symbols_nr = argc - 4;
			}
		}
		else if (argc > 2) {
			symbols = &argv[2];
			symbols_nr = argc - 2;
		}

		stock_price_check_support(date, symbols, symbols_nr);
	}
	else
		goto usage;

	db_close( );

	return 0;

usage:
	db_close( );
	print_usage( );
	return -1;
}
