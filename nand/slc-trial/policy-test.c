// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "policy.h"
int main(void)
{
	unsigned int programs = 0, erases = 0;
	for (unsigned long long offset = 0; offset <= (1ULL << 32); offset += 4096) {
		int in = offset >= SL_TRIAL_START && offset < SL_TRIAL_END;
		int low = sl_test_candidate_lower_index((offset >> 12) & 255) >= 0;
		assert(sl_trial_program_allowed(offset, 4096) == (in && low));
		assert(sl_trial_erase_allowed(offset, 1048576) == (in && offset % 1048576 == 0));
		programs += sl_trial_program_allowed(offset, 4096);
		erases += sl_trial_erase_allowed(offset, 1048576);
		assert(!sl_trial_program_allowed(offset + 1, 4096));
		assert(!sl_trial_erase_allowed(offset + 1, 1048576));
	}
	assert(programs == 4096 && erases == 32);
	assert(!sl_trial_program_allowed(ULLONG_MAX, 4096));
	assert(!sl_trial_erase_allowed(ULLONG_MAX, 1048576));
	for (unsigned int size = 0; size <= 1048577; size++) {
		assert(sl_trial_program_allowed(SL_TRIAL_START, size) == (size == 4096));
		assert(sl_trial_erase_allowed(SL_TRIAL_START, size) == (size == 1048576));
	}
	for (unsigned int logical = 0; logical < 4096; logical++) {
		unsigned int block = logical / 128, rank = logical % 128;
		unsigned long long physical = SL_TRIAL_START + block * 1048576ULL +
			(unsigned int)sl_test_candidate_lower_page(rank) * 4096ULL;
		assert(sl_trial_program_allowed(physical, 4096));
		assert(sl_test_candidate_lower_index((physical >> 12) & 255) == (int)rank);
	}
	puts("PASS: all chip pages, 32 eraseblocks, 4096 candidate lower pages, sizes, overflows, translations");
	return 0;
}
