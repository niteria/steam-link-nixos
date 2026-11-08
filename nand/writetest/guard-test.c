// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "guard.h"

int main(void)
{
	unsigned long long offset;
	/* Every page of the whole chip, and every eraseblock, including endpoints. */
	for (offset = 0; offset <= (1ULL << 32); offset += SL_TEST_PAGESIZE) {
		assert(sl_test_program_allowed(offset, 4096) ==
		       (offset >= SL_TEST_OFFSET && offset < SL_TEST_OFFSET + SL_TEST_ERASESIZE));
		assert(sl_test_erase_allowed(offset, SL_TEST_ERASESIZE) == (offset == SL_TEST_OFFSET));
	}
	for (offset = SL_TEST_OFFSET - 4096; offset <= SL_TEST_OFFSET + 4096; offset++) {
		assert(sl_test_program_allowed(offset, 4096) ==
		       (offset >= SL_TEST_OFFSET && offset % 4096 == 0));
		assert(sl_test_erase_allowed(offset, SL_TEST_ERASESIZE) == (offset == SL_TEST_OFFSET));
	}
	for (unsigned long long length = 0; length <= SL_TEST_ERASESIZE + 4096; length++) {
		assert(sl_test_program_allowed(SL_TEST_OFFSET, length) == (length == 4096));
		assert(sl_test_erase_allowed(SL_TEST_OFFSET, length) == (length == SL_TEST_ERASESIZE));
	}
	assert(!sl_test_program_allowed(ULLONG_MAX, 4096));
	assert(!sl_test_erase_allowed(ULLONG_MAX, SL_TEST_ERASESIZE));
	assert(!sl_test_program_allowed(SL_TEST_OFFSET + (1ULL << 32), 4096));
	assert(!sl_test_erase_allowed(SL_TEST_OFFSET + (1ULL << 32), SL_TEST_ERASESIZE));
	assert(!sl_test_program_allowed(SL_TEST_OFFSET, ULLONG_MAX));
	assert(!sl_test_erase_allowed(SL_TEST_OFFSET, ULLONG_MAX));
	for (unsigned int next = 0; next <= 256; next++)
		for (unsigned int page = 0; page <= 256; page++)
			assert(sl_test_program_next(SL_TEST_OFFSET + page * 4096ULL, 4096, next) ==
			       (next < 256 && next == page));
	for (unsigned int pages = 0; pages <= 257; pages++)
		for (int inspect = 0; inspect <= 1; inspect++)
			for (int erase = 0; erase <= 1; erase++)
				assert(sl_test_hold_allowed(inspect, erase, pages) ==
				       (!inspect && !erase && pages == 256));
	unsigned int count = 0;
	for (unsigned int page = 0; page < 256; page++) {
		int index = sl_test_candidate_lower_index(page);
		if (index >= 0) {
			assert(index == (int)count++);
			assert(sl_test_candidate_lower_page(index) == (int)page);
		}
	}
	assert(count == 128);
	assert(sl_test_candidate_lower_page(128) == -1);
	assert(sl_test_candidate_lower_index(256) == -1);
	puts("PASS: all chip page addresses, byte boundaries, lengths, and overflow guards");
	return 0;
}
