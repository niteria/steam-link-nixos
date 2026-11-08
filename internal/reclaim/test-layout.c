/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "state-core.h"
static unsigned reads;
static int erased(void *unused,unsigned page,unsigned char *raw)
{(void)unused;assert(slr_block_allowed(page/256));reads++;memset(raw,255,SLST_RAW_BYTES);return 0;}
int main(void)
{
 unsigned count=0;unsigned char raw[SLST_RAW_BYTES];struct slst_scan_ops ops={.read=erased};
 for(unsigned block=0;block<4096;block++){
  /* Independent physical ownership oracle, including every protected gap. */
  int expected=(block>=2228&&block<=4085)||(block>=20&&block<=531)||(block>=756&&block<=1139);
  unsigned cursor=77,before=reads;unsigned long long offset=(unsigned long long)block<<20;
  assert(slr_block_allowed(block)==expected);
  assert(sl_trial_erase_allowed(offset,1048576)==expected);
  assert(slst_scan(&ops,block,raw,&cursor)==(expected?0:SLST_SCAN_BOUNDS));
  assert(cursor==(expected?1:77)&&reads-before==(expected?256:0));
  if(expected){int logical=slr_logical_block(block);assert(logical>=0&&slr_physical_block(logical)==(int)block);count++;}
  else assert(slr_logical_block(block)==-1);
  for(unsigned page=0;page<256;page++){
   assert(sl_trial_program_allowed(offset+page*4096,4096)==(expected&&sl_test_candidate_lower_index(page)>=0));
   assert(!sl_trial_program_allowed(offset+page*4096+1,4096));
  }
 }
 assert(count==2754);
 for(unsigned logical=0;logical<2754;logical++){
  int physical=slr_physical_block(logical);assert(physical>=0&&slr_logical_block(physical)==(int)logical);
  if(logical<1858)assert(physical==(int)(2228+logical));
 }
 assert(slr_physical_block(1858)==20&&slr_physical_block(2370)==756);
 assert(slr_physical_block(2754)==-1&&slr_physical_block(UINT_MAX)==-1);
 assert(!sl_trial_program_allowed(~0ULL,4096)&&!sl_trial_erase_allowed(~0ULL,1048576));
 assert(!sl_trial_program_allowed(20ULL<<20,8192)&&!sl_trial_erase_allowed(20ULL<<20,2097152));
 printf("PASS: all4096 physical blocks, all1048576 page permissions, cursor bounds,2754 bijective mappings and unchanged1858-block prefix\n");
 return 0;
}
