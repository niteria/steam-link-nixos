// SPDX-License-Identifier: GPL-2.0-only
/* Stock PE reads mtd1 (bootloader). Expose only that read-only subrange. */
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
static struct mtd_info *parent;
static int partition_index=-1;
static int __init flash_init(void)
{
    struct mtd_info *partition;int result;
    parent=get_mtd_device_nm("steamlink-nand-ro");if(IS_ERR(parent))return PTR_ERR(parent);
    if(parent->index!=0||parent->size!=0x100000000ULL||parent->flags&MTD_WRITEABLE){result=-EPERM;goto put;}
    partition=get_mtd_device(NULL,1);
    if(!IS_ERR(partition)){put_mtd_device(partition);result=-EBUSY;goto put;}
    result=mtd_add_partition(parent,"steamlink-stock-bootloader-ro",0x100000,0x800000);if(result)goto put;
    partition=get_mtd_device_nm("steamlink-stock-bootloader-ro");
    if(IS_ERR(partition)){result=PTR_ERR(partition);mtd_del_partition(parent,1);goto put;}
    partition_index=partition->index;
    result=partition_index==1&&!(partition->flags&MTD_WRITEABLE)?0:-EPERM;
    put_mtd_device(partition);
    if(result){mtd_del_partition(parent,partition_index);goto put;}
    return 0;
put:put_mtd_device(parent);return result;
}
static void __exit flash_exit(void){mtd_del_partition(parent,partition_index);put_mtd_device(parent);}
module_init(flash_init);module_exit(flash_exit);MODULE_LICENSE("GPL");
