#include "util.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

struct stock_vgm
{
	char ticker[8];
	char value, growth, momentum;
};

static int load_vgm_from_file(const char *fname, int *vgm_count, struct stock_vgm *vgm)
{
	char line[1024];
	int i = 0;

	if (!vgm_count || !vgm)
		return -1;

	*vgm_count = 0;

	FILE *file = fopen(fname, "r");
	if (!file) {
		fprintf(stderr, "fopen(%s) failed: %d(%s)\n", fname, errno, strerror(errno));
		return -1;
	}

	while (fgets(line, sizeof(line), file)) {
		char *token;

		token = strtok(line, "\t"); /* name */
		token = strtok(NULL, "\t"); /* ticker */
		strlcpy(vgm[i].ticker, token, sizeof(vgm[i].ticker));

		token = strtok(NULL, "\t"); /* rank */
		token = strtok(NULL, "\t"); /* value */
		vgm[i].value = token[0];

		token = strtok(NULL, "\t"); /* growth */
		vgm[i].growth  = token[0];

		token = strtok(NULL, "\t"); /* momentum */
		vgm[i].momentum = token[0];

		i += 1;
	}

	*vgm_count = i;

	fclose(file);

	return 0;
}

static int momentum_cmp(const void *v1, const void *v2)
{
	const struct stock_vgm *vgm1 = v1;
	const struct stock_vgm *vgm2 = v2;

	return strcmp(vgm1->ticker, vgm2->ticker);
}

void zacks_check_momentum( )
{
	int o_vgm_cnt, n_vgm_cnt;
	struct stock_vgm o_vgm[256], n_vgm[256];
	int i;

	if (load_vgm_from_file("ticker_list/zacks_rank1_vgm.txt", &o_vgm_cnt, o_vgm) < 0
	    || load_vgm_from_file("ticker_list/new_vgm.txt", &n_vgm_cnt, n_vgm) < 0)
		return;

	for (i = 0; i < n_vgm_cnt; i++) {
		struct stock_vgm *n = &n_vgm[i];
		struct stock_vgm *o = bsearch(n, o_vgm, o_vgm_cnt, sizeof(o_vgm[0]), momentum_cmp);
		if (!o)
			printf("%s%s%s is new rank1: value=%c, growth=%c, momentum=%c\n",
				ANSI_COLOR_YELLOW, n->ticker, ANSI_COLOR_RESET, n->value, n->growth, n->momentum);
		else if (n->momentum < o->momentum)
			printf("%s%s%s momentum up: %c --> %c\n", ANSI_COLOR_YELLOW, n->ticker, ANSI_COLOR_RESET, o->momentum, n->momentum);
	}
}
