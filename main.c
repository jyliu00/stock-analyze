#include <stdio.h>
#include <string.h>

static void print_usage(void)
{
	printf("Usage: anna\n");
	printf("       anna {-add | -delete} {symbol-1 symbol-2 ...}\n");
	printf("       anna -update [symbol-1 symbol-2 ...]\n");
	printf("       anna -help\");
	return 0;
}

int main(int argc, const char **argv)
{
	if (argc == 1) { /* do analyze stocks */
	}
	else if (argc == 2) {
		print_usage( );
	}
	else if (!strcmp(argv[1], "-add")) {
	}
	else if (!strcmp(argv[1], "-delete")) {
	}
	else if (!strcmp(argv[1], "-update")) {
	}

	return 0;
}
