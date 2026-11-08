// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "policy.h"
int main(void)
{
	unsigned char map[SL_BBT_MAP_BYTES];
	memset(map, 255, sizeof(map));
	for (unsigned int block = 0; block < 4096; block++) {
		assert(sl_bbt_bad(map, (unsigned long long)block << 20) == 0);
		assert(sl_bbt_reserved((unsigned long long)block << 20) == (block >= 4088));
	}
	for (unsigned int code = 0; code < 4; code++) {
		memset(map, 255, sizeof(map));
		map[90 / 4] &= ~(3U << ((90 % 4) * 2));
		map[90 / 4] |= code << ((90 % 4) * 2);
		for (unsigned int page = 0; page < (1U << 20); page++)
			assert(sl_bbt_bad(map, (unsigned long long)page * 4096) ==
			       (code != 3 && page / 256 == 90));
	}
	assert(sl_bbt_bad(map, 1ULL << 32) == -1);
	assert(sl_bbt_bad(map, ULLONG_MAX) == -1);
	assert(sl_bbt_bad(NULL, 0) == -1);
	assert(sl_bbt_reserved(1ULL << 32) == -1);
	assert(sl_bbt_reserved(ULLONG_MAX) == -1);
	puts("PASS: flash BBT codes, all chip pages, reservations, and invalid offsets");
	return 0;
}
