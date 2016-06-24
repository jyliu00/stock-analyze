#include "fetch_price.h"

#include "sqlite_db.h"
#include "util.h"

#include <stdio.h>

static int normalize_symbol(char *symbol, const char *orig_symbol, int symbol_sz)
{
	int i;

	strlcpy(symbol, orig_symbol, symbol_sz);

	for (i = 0; symbol[i]; i++) {
		if (symbol[i] >= 'a' && symbol[i] <= 'z') {
			symbol[i] -= 'a' - 'A';
		}
	}

	return 0;
}

static int fetch_symbol_price(int fetch_action, const char *orig_symbol)
{
	char symbol[32] = { 0 };
	int symbol_exist;

	if (normalize_symbol(symbol, orig_symbol, sizeof(symbol)) < 0)
		return -1;

	symbol_exist = stock_symbol_exist(symbol);

	switch (fetch_action) {
	case FETCH_ACTION_ADD:
		if (symbol_exist) {
			anna_error("can't add symbol %s, which already exists in db\n", symbol);
			return -1;
		}

		break;

	case FETCH_ACTION_DEL:
		if (!symbol_exist) {
			anna_error("can't del symbol %s, which doesn't exist in db\n", symbol);
			return -1;
		}

		break;

	case FETCH_ACTION_UPDATE:
		if (!symbol_exist) {
			anna_error("can't update symbol %s, which doesn't exist in db\n", symbol);
			return -1;
		}

		break;
	}

	return 0;
}

int fetch_price(int fetch_action, int symbol_nr, const char **symbols)
{
	int i;

	if (symbol_nr) {
		for (i = 0; i < symbol_nr; i++) {
			fetch_symbol_price(fetch_action, symbols[i]);
		}
	}
	else if (fetch_action == FETCH_ACTION_UPDATE) {
	}
	else {
		fprintf(stderr, "fetch_action=%d, symbol_nr=%d\n", fetch_action, symbol_nr);
	}

	return 0;
}
