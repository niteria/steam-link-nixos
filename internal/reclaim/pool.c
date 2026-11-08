// SPDX-License-Identifier: GPL-2.0-only
/* Compose already-SLC-emulated extents; never apply pairing a second time. */
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/concat.h>
#include "layout.h"
static const char *names[]={"steamlink-reclaim-cache","steamlink-reclaim-sysconf","steamlink-reclaim-factory"};
static struct mtd_info *children[SLR_EXTENTS],*pool;
static bool write_enable;
module_param(write_enable,bool,0400);
static int max_bad(struct mtd_info *mtd,loff_t offset,size_t length)
{
 if(offset<0||offset>=mtd->size||length>mtd->size-offset)return -EINVAL;
 /* Report the qualified whole-chip budget. UBI also requires explicit -b60:
  * its per-1024 calculation now sees this smaller logical master, not the
  * original physical chip. Stage1 must assert the resulting reserve. */
 return SLR_BAD_RESERVE;
}
static void release_children(void)
{
 for(unsigned i=0;i<SLR_EXTENTS;i++)if(children[i]){put_mtd_device(children[i]);children[i]=NULL;}
}
static int __init start(void)
{
 int ret;
 for(unsigned i=0;i<SLR_EXTENTS;i++){
  struct mtd_info *m=get_mtd_device_nm(names[i]);
  if(IS_ERR(m)){ret=PTR_ERR(m);goto failed;}
  children[i]=m;
  if(!mtd_is_partition(m)||m->type!=MTD_MLCNANDFLASH||m->size!=((u64)slr_count[i]<<19)||
     m->erasesize!=524288||m->writesize!=4096||m->oobsize!=224||
     (m->flags&~MTD_WRITEABLE)!=MTD_SLC_ON_MLC_EMULATION||
     mtd_get_master_ofs(m,0)!=((u64)slr_first[i]<<20)||
     (write_enable&&!(m->flags&MTD_WRITEABLE))){ret=-EINVAL;goto failed;}
  if(i&&mtd_get_master(m)!=mtd_get_master(children[0])){ret=-EINVAL;goto failed;}
 }
 pool=mtd_concat_create(children,SLR_EXTENTS,"steamlink-reclaimed-root");
 if(!pool){ret=-ENOMEM;goto failed;}
 /* add_mtd_device rejects SLC_ON_MLC on a master. Each child has already
  * halved its size and performs the one required lower-page translation. */
 pool->flags&=~MTD_SLC_ON_MLC_EMULATION;
 if(!write_enable)pool->flags&=~MTD_WRITEABLE;
 pool->type=MTD_NANDFLASH;pool->owner=THIS_MODULE;
 pool->ecc_strength=children[0]->ecc_strength;pool->ecc_step_size=children[0]->ecc_step_size;
 pool->bitflip_threshold=children[0]->bitflip_threshold;pool->_max_bad_blocks=max_bad;
 if(pool->size!=SLR_LOGICAL_BYTES){ret=-EINVAL;goto destroy;}
 ret=mtd_device_register(pool,NULL,0);
 if(ret)goto destroy;
 return 0;
destroy:
 mtd_concat_destroy(pool);pool=NULL;
failed:
 release_children();return ret;
}
static void __exit stop(void)
{
 mtd_device_unregister(pool);mtd_concat_destroy(pool);release_children();
}
module_init(start);module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Appended lower-page NAND extents with pinned children and preserved bad-block reserve");
