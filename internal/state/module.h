/* SPDX-License-Identifier: GPL-2.0-only */
/* Experimental root-only UBI writer: never autoload; qualify on USB first. */
#define slc_armed slst_physical_armed
#define slc_write_page slst_physical_program
#define slc_erase_page slst_physical_erase
#define slc_trial_parts __maybe_unused slst_unused_parts
#define slc_block_isbad slst_physical_block_isbad
#define slc_refuse_markbad __maybe_unused slst_physical_refuse_markbad
#include "steamlink_state_physical.h"
#undef slc_armed
#undef slc_write_page
#undef slc_erase_page
#undef slc_trial_parts
#undef slc_block_isbad
#undef slc_refuse_markbad
#include "state-core.h"
static unsigned long long state_nonce;
static bool state_faulted,state_fresh[4096],state_retired[4096];
static unsigned short state_next[4096];
static unsigned long state_scans,state_programs,state_erases,state_media_faults,state_retirements;
static u8 state_raw[SLST_RAW_BYTES] __aligned(4),state_data[4096] __aligned(4),state_verify[4096] __aligned(4),state_tag[32];
module_param(state_nonce,ullong,0400);
module_param_named(writes_faulted,state_faulted,bool,0400);
module_param_named(scanned_blocks,state_scans,ulong,0400);
module_param_named(programmed_pages,state_programs,ulong,0400);
module_param_named(erased_blocks,state_erases,ulong,0400);
module_param_named(media_faults,state_media_faults,ulong,0400);
module_param_named(retired_blocks,state_retirements,ulong,0400);
static bool slc_armed(void)
{return slst_physical_armed()&&state_nonce&&!state_faulted&&!slh_io.faulted;}
/* Controller/transport state is unknown: stop every later write. The result
 * is deliberately not -EIO, so UBI enters read-only mode instead of retrying. */
static int slst_fault(const char *what,unsigned page,int ret)
{
 state_faulted=true;
 pr_err("steamlink state: writes disabled: %s page=%u block=%u ret=%d\n",what,page,page/256,ret);
 return ret&&ret!=-EIO?ret:-EREMOTEIO;
}
/* A media result for one page or block. -EIO lets UBI move the data to
 * another eraseblock and torture, then reuse or retire, this one. */
static int slst_media(const char *what,unsigned page)
{
 unsigned block=page/256;
 state_next[block]=0;state_fresh[block]=false;state_media_faults++;
 pr_err_ratelimited("steamlink state: %s page=%u block=%u; failing this operation\n",what,page,block);
 return -EIO;
}
static int slst_read_raw(void *context,unsigned page,unsigned char *raw)
{return slh_read_physical(context,page,raw);}
static int slst_prepare(struct nand_chip *chip,unsigned block)
{
 if(!slh_reference_ready){
  int ret=berlin_read_page(chip,state_verify,0);
  if(ret<0||!slh_reference_ready)return slst_fault("reference read",0,ret);
 }
 if(!state_next[block]){
  struct slst_scan_ops ops={.context=chip,.read=slst_read_raw};unsigned cursor=0;
  int ret=slst_scan(&ops,block,state_raw,&cursor);
  if(ret==SLST_SCAN_UPPER)return slst_media("block scan found a non-erased upper page",block*256);
  if(ret)return slst_fault(ret==SLST_SCAN_IO?"block scan read":"block scan bounds",block*256,-EREMOTEIO);
  /* An erase that reported success but left data may have landed elsewhere. */
  if(state_fresh[block]&&cursor!=1)return slst_fault("freshly erased block is not empty",block*256,-EREMOTEIO);
  state_next[block]=cursor;state_scans++;
 }
 return 0;
}
static int slc_erase_page(struct nand_chip *chip,unsigned row)
{
 u64 offset=(u64)row<<12;
 if(!slc_armed()||!sl_trial_erase_allowed(offset,1048576))return -EROFS;
 unsigned block=row/256;
 if(state_retired[block])return -EIO;
 state_next[block]=0;state_fresh[block]=false;
 int ret=slh_hw_stop(chip);
 if(ret)return slst_fault("controller stop before erase",row,ret);
 ret=slst_physical_erase(chip,row);
 int stop=slh_hw_stop(chip);
 if(stop)return slst_fault("controller stop after erase",row,stop);
 if(ret==-EIO)return slst_media("erase status FAIL",row);
 if(ret)return slst_fault("erase",row,ret);
 state_erases++;state_fresh[block]=true;
 return 0;
}
static int slc_write_page(struct nand_chip *chip,const u8 *data,int oob_required,int page)
{
 u64 offset=(u64)page<<12;
 if(!slc_armed()||!sl_trial_program_allowed(offset,4096))return -EROFS;
 if(oob_required&&memchr_inv(chip->oob_poi,255,nand_to_mtd(chip)->oobsize))return -EOPNOTSUPP;
 unsigned block=(unsigned)page/256,rank=sl_test_candidate_lower_index((unsigned)page&255);
 if(state_retired[block])return -EIO;
 /* RAW/reference reads can change NAND scratch/OOB buffers. Preserve the
  * caller's page before reconstructing a previously used eraseblock. */
 memcpy(state_data,data,4096);
 marvell_nfc_select_target(chip,chip->cur_cs);
 int ret=slst_prepare(chip,block);
 if(ret)return ret;
 if(!slst_next_allowed(state_next[block],rank)){
  pr_err("steamlink state: out-of-order program page=%u rank=%u cursor=%u\n",page,rank,state_next[block]);
  return -EPERM;
 }
 ret=slh_read_physical(chip,page,state_raw);
 if(ret)return slst_fault("pre-program raw read",page,ret);
 if(!slst_raw_erased(state_raw))return slst_media("target page is not erased",page);
 state_next[block]=rank+2;state_fresh[block]=false;
 slst_tag(state_tag,page);memset(chip->oob_poi,255,nand_to_mtd(chip)->oobsize);memcpy(chip->oob_poi,state_tag,32);
 ret=slh_hw_stop(chip);
 if(ret)return slst_fault("controller stop before program",page,ret);
 ret=slst_physical_program(chip,state_data,1,page);
 int stop=slh_hw_stop(chip);
 if(stop)return slst_fault("controller stop after program",page,stop);
 if(ret==-EIO)return slst_media("program status FAIL",page);
 if(ret)return slst_fault("program",page,ret);
 unsigned failed=nand_to_mtd(chip)->ecc_stats.failed;
 ret=berlin_read_page(chip,state_verify,page);
 if(ret<0)return slst_fault("verify read",page,ret);
 if(nand_to_mtd(chip)->ecc_stats.failed!=failed||memcmp(state_verify,state_data,4096)||memcmp(chip->oob_poi,state_tag,32)){
  /* A page that still reads erased suggests the program went elsewhere. */
  ret=slh_read_physical(chip,page,state_raw);
  if(ret)return slst_fault("verify raw read",page,ret);
  if(slst_raw_erased(state_raw))return slst_fault("programmed page still reads erased",page,-EREMOTEIO);
  return slst_media("read-back verify mismatch",page);
 }
 state_programs++;return 0;
}
static int slc_block_isbad(struct mtd_info *mtd,loff_t offset)
{
 int bad=slst_physical_block_isbad(mtd,offset);
 if(bad)return bad;
 return offset>=0&&(offset>>20)<4096&&state_retired[offset>>20];
}
/* UBI asks to retire a block after a failed torture or erase. Retirement is
 * held in RAM for this boot; the NAND bad-block tables are never written. */
static int slc_refuse_markbad(struct mtd_info *mtd,loff_t offset)
{
 if(state_faulted||slh_io.faulted)return -EROFS;
 if(offset<0||(offset&1048575)||!sl_trial_erase_allowed(offset,1048576))return -EINVAL;
 unsigned block=offset>>20;
 if(!state_retired[block]){
  state_retired[block]=true;state_next[block]=0;state_fresh[block]=false;state_retirements++;
  pr_err("steamlink state: block %u retired for this boot at UBI request\n",block);
 }
 return 0;
}
static const struct mtd_partition slc_trial_parts[]={
 {.name="steamlink-internal-state-root",.offset=2228ULL<<20,.size=1858ULL<<20,.add_flags=MTD_SLC_ON_MLC_EMULATION},
 {.name="steamlink-state-physical-ro",.offset=2228ULL<<20,.size=1858ULL<<20,.mask_flags=MTD_WRITEABLE},
 {.name="steamlink-state-boot-pools-ro",.offset=2164ULL<<20,.size=64ULL<<20,.mask_flags=MTD_WRITEABLE},
 {.name="steamlink-state-stock-protected-ro",.offset=0,.size=2164ULL<<20,.mask_flags=MTD_WRITEABLE},
 {.name="steamlink-state-metadata-bbt-ro",.offset=4086ULL<<20,.size=10ULL<<20,.mask_flags=MTD_WRITEABLE},
};
