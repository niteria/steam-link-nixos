/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_TEST_PATTERN_H
#define STEAMLINK_TEST_PATTERN_H
/* Shared with the independent stock userspace writer/verifier. */
static inline void sl_test_pattern(unsigned int page, unsigned char *data,
				   unsigned char *spare)
{
	unsigned int state = 0x6d2b79f5U ^ (page * 0x9e3779b9U);
	unsigned int i;
	for (i = 0; i < 4096; i++) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		data[i] = state >> 24;
	}
	for (i = 0; i < 32; i++)
		spare[i] = i < 8 ? 0xff : 0xa5 ^ i ^ page;
}
#endif
