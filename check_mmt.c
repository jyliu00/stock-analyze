#include <stdio.h>
#include <errno.h>
#include <string.h>

void zacks_check_momentum( )
{
	FILE *ofile, *nfile;

	ofile = fopen("ticker_list/zacks_rank1_vgm.txt", "r");
	if (!ofile) {
		fprintf(stderr, "fopen(ticker_list/zacks_rank1_vgm.txt) failed: %d(%s)\n", errno, strerror(errno));
		return;
	}

	nfile = fopen("ticker_list/new_vgm.txt", "r");
	if (!nfile) {
		fprintf(stderr, "fopen(ticker_list/new_vgm.txt) failed: %d(%s)\n", errno, strerror(errno));
		return;
	}
}
