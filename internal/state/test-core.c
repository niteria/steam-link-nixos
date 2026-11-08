/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "erased.h"
static unsigned char pages[256][SLST_RAW_BYTES],scratch[SLST_RAW_BYTES];
static int fail_page=-1;
static int read_page(void *context,unsigned page,unsigned char *raw)
{
 (void)context;unsigned local=page%256;
 if((int)local==fail_page)return -1;
 memcpy(raw,pages[local],SLST_RAW_BYTES);return 0;
}
int main(void)
{
 struct slst_scan_ops ops={.read=read_page};unsigned cursor=0,checks=0;
 memset(pages,255,sizeof(pages));assert(!slst_scan(&ops,2228,scratch,&cursor)&&cursor==1);
 assert(slst_scan(&ops,2227,scratch,&cursor)==SLST_SCAN_BOUNDS);
 assert(slst_scan(&ops,4086,scratch,&cursor)==SLST_SCAN_BOUNDS);
 for(unsigned rank=0;rank<128;rank++){
  unsigned page=sl_test_candidate_lower_page(rank);memset(pages,255,sizeof(pages));
  slst_tag(pages[page]+SLST_METADATA_OFFSET,2228*256+page);
  assert(slst_tag_valid(pages[page]+SLST_METADATA_OFFSET,2228*256+page));
  assert(!slst_raw_erased(pages[page]));
  assert(slh_erased_bits(pages[page],1)<0); /* all-FF data is still programmed */
  assert(!slst_scan(&ops,2228,scratch,&cursor)&&cursor==rank+2);
  assert(!slst_next_allowed(cursor,rank));
  if(rank<127)assert(slst_next_allowed(cursor,rank+1));
  checks+=6;
 }
 memset(pages,255,sizeof(pages));pages[0][0]=0x55;pages[sl_test_candidate_lower_page(17)][0]=0x33;
 assert(!slst_scan(&ops,2228,scratch,&cursor)&&cursor==19); /* sparse legacy prefix */
 for(unsigned page=0;page<256;page++)if(sl_test_candidate_lower_index(page)<0){
  memset(pages,255,sizeof(pages));pages[page][SLST_RAW_BYTES-1]=0xfe;cursor=99;
  assert(slst_scan(&ops,2228,scratch,&cursor)==SLST_SCAN_UPPER&&cursor==99);checks++;
 }
 memset(pages,255,sizeof(pages));fail_page=101;cursor=55;
 assert(slst_scan(&ops,2228,scratch,&cursor)==SLST_SCAN_IO&&cursor==55);fail_page=-1;
 unsigned char tag[32];slst_tag(tag,123456);
 for(unsigned bit=0;bit<256;bit++){
  tag[bit/8]^=1U<<(bit%8);assert(!slst_tag_valid(tag,123456));tag[bit/8]^=1U<<(bit%8);checks++;
 }
 for(unsigned block=0;block<4096;block++){
  unsigned long long offset=(unsigned long long)block<<20;int selected=block>=2228&&block<4086;
  assert(sl_trial_erase_allowed(offset,1048576)==selected);checks++;
  for(unsigned page=0;page<256;page++){
   assert(sl_trial_program_allowed(offset+page*4096,4096)==(selected&&sl_test_candidate_lower_index(page)>=0));checks++;
  }
 }
 assert(!sl_trial_program_allowed(~0ULL,4096));assert(!sl_trial_erase_allowed(~0ULL,1048576));
 printf("PASS: %u state cursor/tag/erasure/aperture checks\n",checks);return 0;
}
