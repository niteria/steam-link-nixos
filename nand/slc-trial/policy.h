/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_SLC_TRIAL_POLICY_H
#define STEAMLINK_SLC_TRIAL_POLICY_H
#include "guard.h"
#define SL_TRIAL_START 0xc0000000ULL
#define SL_TRIAL_END 0xc2000000ULL
#define SL_TRIAL_BLOCKS 32U
static inline int sl_trial_erase_allowed(unsigned long long offset, unsigned long long length)
{
	return length == 1048576 && offset >= SL_TRIAL_START &&
	       offset <= SL_TRIAL_END - 1048576 && offset % 1048576 == 0;
}
static inline int sl_trial_program_allowed(unsigned long long offset, unsigned long long length)
{
	return length == 4096 && offset >= SL_TRIAL_START && offset < SL_TRIAL_END &&
	       offset % 4096 == 0 && sl_test_candidate_lower_index((offset >> 12) & 255) >= 0;
}
#endif
