/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_STATE_CORE_H
#define STEAMLINK_STATE_CORE_H
#include "state-policy.h"
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
#define SLST_RAW_BYTES 4320U
#define SLST_METADATA_OFFSET 4186U
#define SLST_SCAN_IO -1
#define SLST_SCAN_UPPER -2
#define SLST_SCAN_BOUNDS -3
struct slst_scan_ops {void *context;int (*read)(void *,unsigned,unsigned char *);};
static inline int slst_raw_erased(const unsigned char *raw)
{for(unsigned i=0;i<SLST_RAW_BYTES;i++)if(raw[i]!=255)return 0;return 1;}
static inline unsigned slst_get32(const unsigned char *p)
{return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static inline void slst_put32(unsigned char *p,unsigned value)
{for(unsigned i=0;i<4;i++)p[i]=value>>(8*i);}
static inline void slst_tag(unsigned char *metadata,unsigned physical_page)
{
 /* At least128 zero bits distinguish even an all-FF data write from erasure
  * under the existing parity-aware <48-bit erased-page rule. */
 memset(metadata,0,16);memcpy(metadata+16,"SLSTATE1",8);
 slst_put32(metadata+24,physical_page);slst_put32(metadata+28,~physical_page);
}
static inline int slst_tag_valid(const unsigned char *metadata,unsigned physical_page)
{
 for(unsigned i=0;i<16;i++)if(metadata[i])return 0;
 return !memcmp(metadata+16,"SLSTATE1",8)&&slst_get32(metadata+24)==physical_page&&slst_get32(metadata+28)==~physical_page;
}
/* Cursor zero means unknown. Otherwise cursor=first_unused_rank+1. A last
 * consumed rank127 gives129, which accepts no further programs until erase. */
static inline int slst_next_allowed(unsigned cursor,unsigned rank)
{return cursor&&cursor<=128&&rank<128&&rank+1>=cursor;}
static inline int slst_scan(const struct slst_scan_ops *ops,unsigned block,unsigned char *raw,unsigned *cursor)
{
 if(block<2228||block>=4086)return SLST_SCAN_BOUNDS;
 unsigned next=1;
 for(unsigned page=0;page<256;page++){
  if(ops->read(ops->context,block*256+page,raw))return SLST_SCAN_IO;
  if(slst_raw_erased(raw))continue;
  int rank=sl_test_candidate_lower_index(page);
  if(rank<0)return SLST_SCAN_UPPER;
  if((unsigned)rank+2>next)next=rank+2;
 }
 *cursor=next;return 0;
}
#endif
