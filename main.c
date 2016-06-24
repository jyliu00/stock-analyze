#include "sqlite_db.h"
#include "fetch_price.h"

#include <stdio.h>
#include <string.h>

static void print_usage(void)
{
	printf("Usage: anna\n");
	printf("       anna {-add | -delete} {symbol-1 symbol-2 ...}\n");
	printf("       anna -update [symbol-1 symbol-2 ...]\n");
	printf("       anna -help\n");
}

int main(int argc, const char **argv)
{
	if (db_init( ) < 0)
		return -1;

	if (argc < 2) {
		goto usage;
	}
	else if (!strcmp(argv[1], "-add")) {
		if (argc <= 2)
			goto usage;

		fetch_price(FETCH_ACTION_ADD, argc - 2, &argv[2]);
	}
	else if (!strcmp(argv[1], "-delete")) {
		if (argc <= 2)
			goto usage;

		fetch_price(FETCH_ACTION_DEL, argc - 2, &argv[2]);
	}
	else if (!strcmp(argv[1], "-update")) {
		fetch_price(FETCH_ACTION_UPDATE, argc - 2, &argv[2]);
	}

	return 0;

usage:
	print_usage( );
}
