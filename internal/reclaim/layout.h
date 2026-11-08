/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_RECLAIM_LAYOUT_H
#define STEAMLINK_RECLAIM_LAYOUT_H
#include "guard.h"
/* Append new extents: existing UBI physical eraseblock numbers stay unchanged. */
#define SLR_EXTENTS 3U
#define SLR_BLOCKS 2754U
#define SLR_LOGICAL_BYTES (2754ULL << 19)
#define SLR_BAD_RESERVE 160U
static const unsigned slr_first[SLR_EXTENTS]={2228,20,756};
static const unsigned slr_count[SLR_EXTENTS]={1858,512,384};
static inline int slr_block_allowed(unsigned block)
{
 for(unsigned i=0;i<SLR_EXTENTS;i++)
  if(block>=slr_first[i]&&block-slr_first[i]<slr_count[i])return 1;
 return 0;
}
static inline int slr_physical_block(unsigned logical)
{
 for(unsigned i=0;i<SLR_EXTENTS;i++){
  if(logical<slr_count[i])return slr_first[i]+logical;
  logical-=slr_count[i];
 }
 return -1;
}
static inline int slr_logical_block(unsigned physical)
{
 unsigned before=0;
 for(unsigned i=0;i<SLR_EXTENTS;i++){
  if(physical>=slr_first[i]&&physical-slr_first[i]<slr_count[i])return before+physical-slr_first[i];
  before+=slr_count[i];
 }
 return -1;
}
#endif
