/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_WRITETEST_GUARD_H
#define STEAMLINK_WRITETEST_GUARD_H

#define SL_TEST_BLOCK 3072U
#define SL_TEST_OFFSET 0xc0000000ULL
#define SL_TEST_ERASESIZE 0x100000ULL
#define SL_TEST_PAGESIZE 4096U
#define SL_TEST_PAGE (SL_TEST_OFFSET / SL_TEST_PAGESIZE)

/* Exact comparisons avoid overflow, truncation, and partial-block erases.
 * Programming is page-aligned and contained in the one fixed eraseblock. */
static inline int sl_test_erase_allowed(unsigned long long offset,
				       unsigned long long length)
{
	return offset == SL_TEST_OFFSET && length == SL_TEST_ERASESIZE;
}

static inline int sl_test_program_allowed(unsigned long long offset,
					 unsigned long long length)
{
	return offset >= SL_TEST_OFFSET && offset < SL_TEST_OFFSET + SL_TEST_ERASESIZE &&
	       (offset - SL_TEST_OFFSET) % SL_TEST_PAGESIZE == 0 && length == SL_TEST_PAGESIZE;
}

static inline int sl_test_program_next(unsigned long long offset,
				      unsigned long long length, unsigned int next)
{
	return next < 256 && sl_test_program_allowed(offset, length) &&
	       offset == SL_TEST_OFFSET + (unsigned long long)next * SL_TEST_PAGESIZE;
}

static inline int sl_test_hold_allowed(int inspect, int erase, unsigned int pages)
{
	return !inspect && !erase && pages == 256;
}

/* Experimental lower-page candidates derived from this unit's program-time
 * profile, not yet a qualified manufacturer pairing scheme. */
static inline int sl_test_candidate_lower_page(unsigned int index)
{
	if (index >= 128)
		return -1;
	return index < 4 ? (int)index : (int)((index / 2) * 4 - 2 + index % 2);
}

static inline int sl_test_candidate_lower_index(unsigned int page)
{
	if (page < 4)
		return page;
	if (page < 6 || page > 251 || (page % 4 != 2 && page % 4 != 3))
		return -1;
	return ((page + 2) / 4) * 2 + page % 2;
}
#endif
