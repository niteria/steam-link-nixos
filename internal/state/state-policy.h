/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_STATE_POLICY_H
#define STEAMLINK_STATE_POLICY_H
#include "guard.h"
#define SL_TRIAL_START (2228ULL << 20)
#define SL_TRIAL_END (4086ULL << 20)
#define SL_TRIAL_BLOCKS 1858U
static inline int sl_trial_erase_allowed(unsigned long long offset,unsigned long long length)
{return length==1048576&&!(offset&1048575)&&offset>=SL_TRIAL_START&&offset<=SL_TRIAL_END-1048576;}
static inline int sl_trial_program_allowed(unsigned long long offset,unsigned long long length)
{return length==4096&&!(offset&4095)&&offset>=SL_TRIAL_START&&offset<SL_TRIAL_END&&sl_test_candidate_lower_index((offset>>12)&255)>=0;}
#endif
