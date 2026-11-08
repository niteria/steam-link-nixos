/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_PARITY_ERASED_H
#define STEAMLINK_PARITY_ERASED_H
#define SLH_RAW_SIZE 4320U
#define SLH_CHUNK0_SIZE 2138U /* 2048 data + 90 parity */
#define SLH_CHUNK1_SIZE 2170U /* 2048 data + 32 metadata + 90 parity */
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
/* Physical stream: data2048, parity90, data2048, metadata32, parity90, pad12.
 * Never classify corrected data alone: an interrupted program may have left
 * substantial parity programming even when the data bytes still resemble FF. */
static inline int slh_erased_bits(const unsigned char *raw, unsigned chunk)
{
 unsigned count = 0, begin, end;
 if (chunk > 1) return -1;
 begin = chunk ? SLH_CHUNK0_SIZE : 0;
 end = begin + (chunk ? SLH_CHUNK1_SIZE : SLH_CHUNK0_SIZE);
 for (unsigned i = begin; i < end; i++) {
  unsigned byte = (unsigned char)~raw[i];
  while (byte) { count += byte & 1; byte >>= 1; }
  /* Keep the prior conservative <48 rule; do not silently expand acceptance. */
  if (count >= 48) return -1;
 }
 return (int)count;
}
static inline int slh_normalize_erased(const unsigned char *raw, unsigned char *data,
                                       unsigned char *metadata, unsigned chunk)
{
 int bits = slh_erased_bits(raw, chunk);
 if (bits < 0) return bits;
 memset(data + chunk * 2048, 255, 2048);
 if (chunk) memset(metadata, 255, 32);
 return bits;
}
#endif
