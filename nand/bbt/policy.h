/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_BBT_POLICY_H
#define STEAMLINK_BBT_POLICY_H
#define SL_BBT_BLOCKS 4096U
#define SL_BBT_RESERVED_START 4088U
#define SL_BBT_MAP_BYTES 1024U
static inline int sl_bbt_bad(const unsigned char *map, unsigned long long offset)
{
	unsigned int block;
	if (!map || offset >= (1ULL << 32))
		return -1;
	block = offset >> 20;
	/* Stock flash encoding: 11 good, 00 factory-bad, 01/10 worn-bad. */
	return ((map[block / 4] >> ((block % 4) * 2)) & 3) != 3;
}
static inline int sl_bbt_reserved(unsigned long long offset)
{
	if (offset >= (1ULL << 32))
		return -1;
	return (offset >> 20) >= SL_BBT_RESERVED_START;
}
#endif
