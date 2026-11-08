/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_RECLAIM_POLICY_H
#define STEAMLINK_RECLAIM_POLICY_H
#include "layout.h"
/* Distinct explicit admission from the old contiguous 1858-block writer. */
#define SL_TRIAL_START (20ULL << 20)
#define SL_TRIAL_END (4086ULL << 20)
#define SL_TRIAL_BLOCKS SLR_BLOCKS
static inline int sl_trial_erase_allowed(unsigned long long offset,unsigned long long length)
{
 return length==1048576&&!(offset&1048575)&&offset<(4096ULL<<20)&&slr_block_allowed(offset>>20);
}
static inline int sl_trial_program_allowed(unsigned long long offset,unsigned long long length)
{
 return length==4096&&!(offset&4095)&&offset<(4096ULL<<20)&&slr_block_allowed(offset>>20)&&
   sl_test_candidate_lower_index((offset>>12)&255)>=0;
}
#endif
