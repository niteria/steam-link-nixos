// SPDX-License-Identifier: GPL-2.0-only
/* ARM32 SHM/CC foundations. No PE/MMIO/DMA engine and no autoload binding. */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/overflow.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/scatterlist.h>
#include <linux/iosys-map.h>
#include <linux/file.h>
#include <net/sock.h>
#include <net/netlink.h>
#include "core.h"
#include "layout.h"

static bool enable, test_backing;
static unsigned int cache_dma_mib=192;
module_param(enable, bool, 0400);
module_param(test_backing, bool, 0400);
module_param(cache_dma_mib, uint, 0400);
MODULE_PARM_DESC(enable,"Explicitly enable this experimental foundation module");
MODULE_PARM_DESC(test_backing,"VM-only CPU backing; never maps physical AV RAM");
MODULE_PARM_DESC(cache_dma_mib,"DMA-managed cache bank MiB, 64..192 in multiples of 4; fixed at module load");
static DEFINE_MUTEX(lock);
static struct ag_heap heaps[2];
static struct ag_registry registry;
static void *backing[2];
static void __iomem *physical_backing[2];
struct cbuf { ag_u32 id,offset,length,refs; };
static struct cbuf cbufs[AG_SERVICES];
static ag_u32 next_owner;
static struct class *av_class;
static struct cdev shm_cdev, cc_cdev;
static struct sock *cc_socket;
static struct workqueue_struct *message_queue;
struct session { ag_u32 owner; int heap; ag_u32 cbuf_refs[AG_SERVICES]; };
static struct device *cache_dma_device;
static void *cache_dma_cpu;
static dma_addr_t cache_dma_address;

#if IS_ENABLED(CONFIG_DMA_SHARED_BUFFER) && IS_ENABLED(CONFIG_DMA_CMA)
MODULE_IMPORT_NS("DMA_BUF");
static bool cache_dma_registered;
struct shm_export {
    struct list_head link;
    struct file *origin;
    ag_u32 offset,length;
};
static LIST_HEAD(shm_exports);
static int cache_dma_probe(struct platform_device *pdev)
{
    int ret;
    if(cache_dma_device)return -EBUSY;
    ret=dma_set_mask_and_coherent(&pdev->dev,DMA_BIT_MASK(32));
    if(ret)return ret;
    ret=of_reserved_mem_device_init(&pdev->dev);
    if(ret)return ret;
    cache_dma_cpu=dma_alloc_wc(&pdev->dev,heaps[1].size,&cache_dma_address,GFP_KERNEL);
    if(!cache_dma_cpu){of_reserved_mem_device_release(&pdev->dev);return -ENOMEM;}
    if(cache_dma_address!=heaps[1].base){
        dma_free_wc(&pdev->dev,heaps[1].size,cache_dma_cpu,cache_dma_address);
        cache_dma_cpu=NULL;of_reserved_mem_device_release(&pdev->dev);return -ERANGE;
    }
    cache_dma_device=&pdev->dev;
    pr_info("steamlink-av: DMA-managed cache heap %pad size %#x, page metadata %d\n",
            &cache_dma_address,heaps[1].size,pfn_valid(cache_dma_address>>PAGE_SHIFT));
    return 0;
}
static void cache_dma_remove(struct platform_device *pdev)
{
    dma_free_wc(&pdev->dev,heaps[1].size,cache_dma_cpu,cache_dma_address);
    cache_dma_cpu=NULL;cache_dma_device=NULL;
    of_reserved_mem_device_release(&pdev->dev);
}
static const struct of_device_id cache_dma_match[]={
    {.compatible="valve,steamlink-shm-dma"},{}
};
static struct platform_driver cache_dma_driver={
    .probe=cache_dma_probe,.remove=cache_dma_remove,
    .driver={.name="steamlink-shm-dma",.of_match_table=cache_dma_match,.suppress_bind_attrs=true,.probe_type=PROBE_FORCE_SYNCHRONOUS},
};
static int cache_dma_prepare(void)
{
    int ret=platform_driver_register(&cache_dma_driver);
    if(ret)return ret;
    cache_dma_registered=true;
    return cache_dma_cpu?0:-ENODEV;
}
static void cache_dma_cleanup(void)
{
    if(cache_dma_registered){platform_driver_unregister(&cache_dma_driver);cache_dma_registered=false;}
}
/* Called under the metadata lock. The exporting file remains referenced until
 * the last DMA-BUF/import/VMA reference goes away. Explicit free is refused. */
static bool allocation_exported(int heap,ag_u32 offset)
{
    struct shm_export *item;
    if(heap!=1)return false;
    list_for_each_entry(item,&shm_exports,link)if(item->offset==offset)return true;
    return false;
}
static struct sg_table *shm_dma_map(struct dma_buf_attachment *attach,enum dma_data_direction direction)
{
    struct shm_export *item=attach->dmabuf->priv;
    struct sg_table *table=kzalloc(sizeof(*table),GFP_KERNEL);
    int ret;
    if(!table)return ERR_PTR(-ENOMEM);
    ret=sg_alloc_table(table,1,GFP_KERNEL);
    if(ret){kfree(table);return ERR_PTR(ret);}
    sg_set_page(table->sgl,pfn_to_page((cache_dma_address+item->offset)>>PAGE_SHIFT),item->length,0);
    ret=dma_map_sgtable(attach->dev,table,direction,DMA_ATTR_SKIP_CPU_SYNC);
    if(ret){sg_free_table(table);kfree(table);return ERR_PTR(ret);}
    return table;
}
static void shm_dma_unmap(struct dma_buf_attachment *attach,struct sg_table *table,enum dma_data_direction direction)
{
    dma_unmap_sgtable(attach->dev,table,direction,DMA_ATTR_SKIP_CPU_SYNC);
    sg_free_table(table);kfree(table);
}
static int shm_dma_mmap(struct dma_buf *buffer,struct vm_area_struct *vma)
{
    struct shm_export *item=buffer->priv;
    unsigned long bytes=vma->vm_end-vma->vm_start;
    if(vma->vm_pgoff || bytes>item->length || (vma->vm_flags&VM_EXEC) || !(vma->vm_flags&VM_SHARED))return -EINVAL;
    vm_flags_clear(vma,VM_MAYEXEC);vm_flags_set(vma,VM_DONTEXPAND|VM_DONTDUMP);
    return dma_mmap_wc(cache_dma_device,vma,cache_dma_cpu+item->offset,cache_dma_address+item->offset,item->length);
}
static int shm_dma_vmap(struct dma_buf *buffer,struct iosys_map *map)
{
    struct shm_export *item=buffer->priv;
    iosys_map_set_vaddr(map,cache_dma_cpu+item->offset);return 0;
}
static void shm_dma_vunmap(struct dma_buf *buffer,struct iosys_map *map)
{(void)buffer;iosys_map_clear(map);}
static int shm_dma_begin(struct dma_buf *buffer,enum dma_data_direction direction)
{
    long ret=dma_resv_wait_timeout(buffer->resv,direction==DMA_FROM_DEVICE?DMA_RESV_USAGE_WRITE:DMA_RESV_USAGE_READ,true,MAX_SCHEDULE_TIMEOUT);
    if(ret<0)return ret;
    mb();return ret?0:-ETIMEDOUT;
}
static int shm_dma_end(struct dma_buf *buffer,enum dma_data_direction direction)
{(void)buffer;(void)direction;mb();return 0;}
static void shm_dma_release(struct dma_buf *buffer)
{
    struct shm_export *item=buffer->priv;
    mutex_lock(&lock);list_del(&item->link);mutex_unlock(&lock);
    fput(item->origin);kfree(item);
}
static const struct dma_buf_ops shm_dma_ops={
    .map_dma_buf=shm_dma_map,.unmap_dma_buf=shm_dma_unmap,.mmap=shm_dma_mmap,
    .vmap=shm_dma_vmap,.vunmap=shm_dma_vunmap,.begin_cpu_access=shm_dma_begin,
    .end_cpu_access=shm_dma_end,.release=shm_dma_release,
};
static long shm_export_buffer(struct file *file,void __user *user)
{
    struct session *session=file->private_data;
    struct ag_dmabuf request;
    struct ag_block block={0};
    struct shm_export *item;
    struct dma_buf *buffer;
    DEFINE_DMA_BUF_EXPORT_INFO(info);
    int fd,ret=-EINVAL;
    if(!cache_dma_cpu || session->heap!=1)return -EOPNOTSUPP;
    if(copy_from_user(&request,user,sizeof(request)))return -EFAULT;
    if(request.flags || !request.length)return -EINVAL;
    item=kzalloc(sizeof(*item),GFP_KERNEL);if(!item)return -ENOMEM;
    fd=get_unused_fd_flags(O_CLOEXEC);if(fd<0){kfree(item);return fd;}
    mutex_lock(&lock);
    for(unsigned i=0;i<heaps[1].count;i++){
        struct ag_block *candidate=&heaps[1].blocks[i];
        if(request.offset<candidate->offset || request.offset-candidate->offset>=candidate->length)continue;
        if(candidate->owner!=session->owner){ret=-EACCES;goto fail_locked;}
        if(request.length>candidate->length-(request.offset-candidate->offset))goto fail_locked;
        block=*candidate;break;
    }
    if(!block.length || (block.offset&~PAGE_MASK) || (block.length&~PAGE_MASK))goto fail_locked;
    item->offset=block.offset;item->length=block.length;item->origin=get_file(file);
    info.exp_name="steamlink-display";info.owner=THIS_MODULE;info.ops=&shm_dma_ops;
    info.size=block.length;info.flags=O_RDWR;info.priv=item;
    buffer=dma_buf_export(&info);
    if(IS_ERR(buffer)){ret=PTR_ERR(buffer);mutex_unlock(&lock);fput(item->origin);goto fail;}
    list_add(&item->link,&shm_exports);
    mutex_unlock(&lock);
    request.fd=fd;request.plane_offset=request.offset-block.offset;request.allocation_size=block.length;
    if(copy_to_user(user,&request,sizeof(request))){put_unused_fd(fd);dma_buf_put(buffer);return -EFAULT;}
    fd_install(fd,buffer->file);return 0;
fail_locked:
    mutex_unlock(&lock);
fail:
    put_unused_fd(fd);kfree(item);return ret;
}
#else
static int cache_dma_prepare(void){return -EOPNOTSUPP;}
static void cache_dma_cleanup(void){}
static bool allocation_exported(int heap,ag_u32 offset){(void)heap;(void)offset;return false;}
static long shm_export_buffer(struct file *file,void __user *user){(void)file;(void)user;return -EOPNOTSUPP;}
#endif
/* The cached heap uses WC without dirty CPU aliases. The legacy noncache heap
 * retains Device/strong ordering for descriptor/doorbell protocols which don't
 * call cache ioctls. Kernel and user mappings always use matching attributes. */
static void heap_write(unsigned int heap,ag_u32 offset,const void *data,size_t length)
{
    if(test_backing)memcpy(backing[heap]+offset,data,length);
    else if(heap==1&&cache_dma_cpu)memcpy(cache_dma_cpu+offset,data,length);
    else memcpy_toio(physical_backing[heap]+offset,data,length);
    wmb();
}
static void heap_zero(unsigned int heap,ag_u32 offset,size_t length)
{
    if(test_backing)memset(backing[heap]+offset,0,length);
    else if(heap==1&&cache_dma_cpu)memset(cache_dma_cpu+offset,0,length);
    else memset_io(physical_backing[heap]+offset,0,length);
}
static void cbuf_drop(struct session *s,unsigned int index)
{
    struct cbuf *c=&cbufs[index];
    struct ag_cbuf_service service;
    if(!s->cbuf_refs[index])return;
    s->cbuf_refs[index]--;c->refs--;
    if(c->refs){
        int entry=ag_find_service(&registry,c->id);
        memcpy(&service,&registry.entries[entry].value,sizeof(service));service.refs=c->refs;
        memcpy(&registry.entries[entry].value,&service,sizeof(service));
    }else{
        ag_unregister(&registry,AG_BAD,c->id);ag_free(&heaps[1],AG_BAD,c->offset);
        memset(c,0,sizeof(*c));
    }
}
static long cbuf_create(struct session *s,struct ag_cbuf_service *service,unsigned int *slot)
{
    struct ag_cbuf_service saved;
    ag_u32 length,offset;
    unsigned int i;
    if(service->id!=AG_DYNAMIC){
        int entry=ag_find_service(&registry,service->id);
        if(entry>=0){
            for(i=0;i<AG_SERVICES;i++)if(cbufs[i].id==service->id)break;
            if(i==AG_SERVICES)return AG_E_EXISTS;
            if(cbufs[i].refs==AG_BAD)return AG_E_FULL;
            memcpy(&saved,&registry.entries[entry].value,sizeof(saved));
            cbufs[i].refs++;s->cbuf_refs[i]++;saved.refs=cbufs[i].refs;
            memcpy(&registry.entries[entry].value,&saved,sizeof(saved));*service=saved;*slot=i;return 0;
        }
    }
    if(!service->size || check_add_overflow((ag_u32)sizeof(struct ag_cbuf_body),service->size,&length) ||
       check_add_overflow(length,service->end_size,&length) ||
       ((service->flags&1) && check_add_overflow(length,64U,&length)))return AG_E_BADVALUE;
    for(i=0;i<AG_SERVICES;i++)if(!cbufs[i].refs)break;
    if(i==AG_SERVICES)return AG_E_FULL;
    offset=ag_allocate(&heaps[1],AG_BAD,length,64);
    if(offset==AG_BAD)return -ENOMEM;
    service->level=2;service->type=2;service->pad1=0;service->pad2=0;service->refs=1;service->offset=offset;
    struct ag_service wire;memcpy(&wire,service,sizeof(wire));
    long result=ag_register(&registry,AG_BAD,&wire);
    if(result){ag_free(&heaps[1],AG_BAD,offset);return result;}
    service->id=wire.id;
    struct ag_cbuf_body body={.start=(service->flags&1)?64:sizeof(body),.size=service->size,
                             .end_size=service->end_size,.flags=service->flags};
    heap_zero(1,offset,length);heap_write(1,offset,&body,sizeof(body));
    cbufs[i]=(struct cbuf){service->id,offset,length,1};s->cbuf_refs[i]++;*slot=i;return 0;
}
static long service_update(struct session *s,const struct ag_service *service)
{
    unsigned int i;
    for(i=0;i<AG_SERVICES;i++)if(cbufs[i].refs&&cbufs[i].id==service->id)break;
    if(i==AG_SERVICES)return ag_update(&registry,s->owner,service);
    if(!s->cbuf_refs[i])return AG_E_ACCESS;
    int entry=ag_find_service(&registry,service->id);
    if(entry<0)return AG_E_MISSING;
    struct ag_cbuf_service canonical,requested;
    memcpy(&canonical,&registry.entries[entry].value,sizeof(canonical));
    memcpy(&requested,service,sizeof(requested));
    /* The vendor CBuf wrapper publishes its MsgQ after CREATE_CBUF. Holders
     * may change only notice; geometry and refcounts remain kernel-owned. */
    ag_u32 notice=requested.notice;
    requested.notice=canonical.notice;requested.refs=canonical.refs;
    if(memcmp(&requested,&canonical,sizeof(canonical)))return AG_E_BADVALUE;
    canonical.notice=notice;
    struct ag_service updated;memcpy(&updated,&canonical,sizeof(updated));
    return ag_update(&registry,AG_BAD,&updated);
}
static int av_open(struct inode *inode,struct file *file)
{
    struct session *s=kzalloc(sizeof(*s),GFP_KERNEL);
    if(!s)return -ENOMEM;
    mutex_lock(&lock);
    if(next_owner==AG_BAD){mutex_unlock(&lock);kfree(s);return -EMFILE;}
    s->owner=++next_owner;
    s->heap=imajor(inode)==246?(int)iminor(inode):-1;
    mutex_unlock(&lock);
    if(s->heap>1){kfree(s);return -ENODEV;}
    file->private_data=s;return nonseekable_open(inode,file);
}
static int av_release(struct inode *inode,struct file *file)
{
    struct session *s=file->private_data;
    (void)inode;
    mutex_lock(&lock);
    if(s->heap>=0)ag_heap_release(&heaps[s->heap],s->owner);
    else {
        for(unsigned int i=0;i<AG_SERVICES;i++)while(s->cbuf_refs[i])cbuf_drop(s,i);
        ag_registry_release(&registry,s->owner);
    }
    mutex_unlock(&lock);kfree(s);return 0;
}
static long shm_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
    struct session *s=file->private_data;
    struct ag_heap *h=&heaps[s->heap];
    void __user *user=(void __user *)arg;
    struct ag_operation op;
    long result=0;
    if(cmd==AG_SHM_EXPORT)return shm_export_buffer(file,user);
    /* mmap takes mm's lock before the metadata lock. Keep that order here. */
    if(cmd==AG_SHM_CLEAN||cmd==AG_SHM_INVALIDATE||cmd==AG_SHM_FLUSH){
        struct vm_area_struct *vma;
        unsigned long start,end;
        if(copy_from_user(&op,user,sizeof(op)))return -EFAULT;
        start=op.p1;
        if(!op.p2 || check_add_overflow(start,(unsigned long)op.p2,&end))return -EINVAL;
        mmap_read_lock(current->mm);
        vma=find_vma(current->mm,start);
        if(!vma || vma->vm_start>start || end>vma->vm_end || vma->vm_file!=file){result=-EINVAL;}
        else if(vma->vm_pgoff>h->size/PAGE_SIZE || start-vma->vm_start>h->size-(vma->vm_pgoff<<PAGE_SHIFT))result=-EINVAL;
        else if(op.p3!=h->base+(vma->vm_pgoff<<PAGE_SHIFT)+start-vma->vm_start ||
                op.p2>h->size-(vma->vm_pgoff<<PAGE_SHIFT)-(start-vma->vm_start))result=-EINVAL;
        else {mb();result=0;}
        mmap_read_unlock(current->mm);return result;
    }
    mutex_lock(&lock);
    if(cmd==AG_SHM_INFO){struct ag_meminfo info;ag_heap_info(h,&info);result=copy_to_user(user,&info,sizeof(info))?-EFAULT:0;}
    else if(cmd==AG_SHM_BASE){struct ag_baseinfo info={h->size,64,h->base,0,0};result=copy_to_user(user,&info,sizeof(info))?-EFAULT:0;}
    else if(cmd==AG_SHM_ALLOC||cmd==AG_SHM_FREE){
        if(copy_from_user(&op,user,sizeof(op))){result=-EFAULT;goto out;}
        if(cmd==AG_SHM_ALLOC){
            op.p1=ag_allocate(h,s->owner,op.p1,op.p2);
            if(copy_to_user(user,&op,sizeof(op))){if(op.p1!=AG_BAD)ag_free(h,s->owner,op.p1);result=-EFAULT;}
        }else{
            op.p1=allocation_exported(s->heap,op.p1)?(ag_u32)-EBUSY:(ag_free(h,s->owner,op.p1)?(ag_u32)-EINVAL:0);
            result=copy_to_user(user,&op,sizeof(op))?-EFAULT:0;
        }
    }else result=-ENOTTY;
out:
    mutex_unlock(&lock);return result;
}
static int shm_mmap(struct file *file,struct vm_area_struct *vma)
{
    struct session *s=file->private_data;
    struct ag_heap *h=&heaps[s->heap];
    unsigned long bytes=vma->vm_end-vma->vm_start;
    if(vma->vm_pgoff || bytes>U32_MAX || !ag_map_valid(h,0,(ag_u32)bytes))return -EINVAL;
    if((vma->vm_flags&VM_EXEC)||!(vma->vm_flags&VM_SHARED))return -EACCES;
    vm_flags_clear(vma,VM_MAYEXEC);
    vm_flags_set(vma,VM_DONTEXPAND|VM_DONTDUMP);
    /* vm_file's reference pins this open file/session until the last VMA and
     * duplicated descriptor close, including forked mappings. */
    if(test_backing)return remap_vmalloc_range(vma,backing[s->heap],0);
    if(s->heap==1&&cache_dma_cpu)return dma_mmap_wc(cache_dma_device,vma,cache_dma_cpu,cache_dma_address,h->size);
    vm_flags_set(vma,VM_IO);
    vma->vm_page_prot=s->heap==0?pgprot_noncached(vma->vm_page_prot):pgprot_writecombine(vma->vm_page_prot);
    return remap_pfn_range(vma,vma->vm_start,h->base>>PAGE_SHIFT,bytes,vma->vm_page_prot);
}
static int send_message(ag_u32 id,const struct ag_message *message)
{
    if(!cc_socket)return -ENODEV;
    struct sk_buff *skb=nlmsg_new(sizeof(*message),GFP_KERNEL);
    struct nlmsghdr *header;
    if(!skb)return -ENOMEM;
    header=nlmsg_put(skb,0,0,NLMSG_DONE,sizeof(*message),0);
    if(!header){kfree_skb(skb);return -EMSGSIZE;}
    NETLINK_CB(skb).portid=0;
    NETLINK_CB(skb).dst_group=0;
    memcpy(nlmsg_data(header),message,sizeof(*message));
    return netlink_unicast(cc_socket,skb,id,MSG_DONTWAIT)<0?-EIO:0;
}
/* SDK kernel ABI used by the PE/DHUB adapter. Physical aliases have the same
 * non-cacheable attributes as userspace; address conversion is IRQ-safe. */
void *MV_SHM_GetCacheVirtAddr(size_t offset)
{if(offset>=heaps[1].size)return NULL;return test_backing?backing[1]+offset:cache_dma_cpu?cache_dma_cpu+offset:(void __force *)physical_backing[1]+offset;}
EXPORT_SYMBOL_GPL(MV_SHM_GetCacheVirtAddr);
void *MV_SHM_GetNonCacheVirtAddr(size_t offset)
{if(offset>=heaps[0].size)return NULL;return test_backing?backing[0]+offset:(void __force *)physical_backing[0]+offset;}
EXPORT_SYMBOL_GPL(MV_SHM_GetNonCacheVirtAddr);
void *MV_SHM_GetCachePhysAddr(size_t offset)
{return offset<heaps[1].size?(void *)(unsigned long)(heaps[1].base+offset):NULL;}
EXPORT_SYMBOL_GPL(MV_SHM_GetCachePhysAddr);
void *MV_SHM_GetNonCachePhysAddr(size_t offset)
{return offset<heaps[0].size?(void *)(unsigned long)(heaps[0].base+offset):NULL;}
EXPORT_SYMBOL_GPL(MV_SHM_GetNonCachePhysAddr);
struct queued_message { struct work_struct work; ag_u32 service; struct ag_message message; };
static void post_message(struct work_struct *work)
{
    struct queued_message *q=container_of(work,struct queued_message,work);
    struct ag_service service={.id=q->service};
    mutex_lock(&lock);bool exists=!ag_inquire(&registry,&service);mutex_unlock(&lock);
    if(exists)send_message(q->service,&q->message);
    kfree(q);
}
int MV_CC_MsgQ_PostMsgByID(ag_u32 service,void *message)
{
    struct queued_message *q;
    if(!message)return -EINVAL;
    q=kmalloc(sizeof(*q),GFP_ATOMIC);if(!q)return -ENOMEM;
    q->service=service;memcpy(&q->message,message,sizeof(q->message));INIT_WORK(&q->work,post_message);
    queue_work(message_queue,&q->work);return 0;
}
EXPORT_SYMBOL_GPL(MV_CC_MsgQ_PostMsgByID);
static long cc_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
    struct session *s=file->private_data;
    struct ag_service service;
    void __user *user=(void __user *)arg;
    long result=0;
    mutex_lock(&lock);
    if(cmd==AG_CC_STATUS){result=copy_to_user(user,&registry.status,sizeof(registry.status))?-EFAULT:0;goto out;}
    if(cmd==AG_CC_LIST){
        struct ag_list head,*list;unsigned int count;
        if(copy_from_user(&head,user,sizeof(head))){result=-EFAULT;goto out;}
        if(!head.capacity||head.capacity>AG_SERVICES){result=AG_E_BADVALUE;goto out;}
        count=min(head.capacity,registry.status.count);
        list=kzalloc(sizeof(*list)+count*sizeof(service),GFP_KERNEL);
        if(!list){result=-ENOMEM;goto out;}
        list->capacity=head.capacity;list->count=count;list->total=registry.status.count;
        for(unsigned int i=0;i<count;i++)list->entries[i]=registry.entries[i].value;
        result=copy_to_user(user,list,sizeof(*list)+count*sizeof(service))?-EFAULT:0;kfree(list);goto out;
    }
    if(cmd==AG_CC_CREATE_CBUF||cmd==AG_CC_DESTROY_CBUF){
        struct ag_cbuf_service c;unsigned int index;
        if(copy_from_user(&c,user,sizeof(c))){result=-EFAULT;goto out;}
        if(cmd==AG_CC_CREATE_CBUF){
            result=cbuf_create(s,&c,&index);
            if(!result&&copy_to_user(user,&c,sizeof(c))){cbuf_drop(s,index);result=-EFAULT;}
        }else{
            for(index=0;index<AG_SERVICES;index++)if(cbufs[index].id==c.id&&s->cbuf_refs[index])break;
            if(index==AG_SERVICES){result=AG_E_ACCESS;goto out;}
            int entry=ag_find_service(&registry,c.id);
            memcpy(&c,&registry.entries[entry].value,sizeof(c));c.refs--;
            /* A failed copy must not silently consume the caller's reference. */
            if(copy_to_user(user,&c,sizeof(c))){result=-EFAULT;goto out;}
            cbuf_drop(s,index);
        }
        goto out;
    }
    if(cmd!=AG_CC_REG&&cmd!=AG_CC_FREE&&cmd!=AG_CC_INQUIRY&&cmd!=AG_CC_UPDATE&&cmd!=AG_CC_TEST_MSG){result=-ENOTTY;goto out;}
    if(copy_from_user(&service,user,sizeof(service))){result=-EFAULT;goto out;}
    switch(cmd){
    case AG_CC_REG:
        result=ag_register(&registry,s->owner,&service);
        if(!result&&copy_to_user(user,&service,sizeof(service))){ag_unregister(&registry,s->owner,service.id);result=-EFAULT;}
        break;
    case AG_CC_FREE:result=ag_unregister(&registry,s->owner,service.id);break;
    case AG_CC_INQUIRY:
        result=ag_inquire(&registry,&service);
        if(!result&&copy_to_user(user,&service,sizeof(service)))result=-EFAULT;
        break;
    case AG_CC_UPDATE:result=service_update(s,&service);break;
    case AG_CC_TEST_MSG:{
        struct ag_message message={service.id,service.data[0],service.data[1]};
        result=ag_inquire(&registry,&service);
        mutex_unlock(&lock);
        return result?result:send_message(message.id,&message);
    }
    }
out:
    mutex_unlock(&lock);return result;
}
static const struct file_operations shm_ops={.owner=THIS_MODULE,.open=av_open,.release=av_release,.unlocked_ioctl=shm_ioctl,.mmap=shm_mmap};
static const struct file_operations cc_ops={.owner=THIS_MODULE,.open=av_open,.release=av_release,.unlocked_ioctl=cc_ioctl};
static char *node_mode(const struct device *dev,umode_t *mode){(void)dev;if(mode)*mode=0600;return NULL;}
static void ignore_input(struct sk_buff *skb){(void)skb;}
static int prepare_heaps(void)
{
    const ag_u32 base[2]={0x1e000000,0x12000000},size[2]={0x02000000,0x0c000000};
    const char *path[2]={"/reserved-memory/av-noncache@1e000000","/reserved-memory/av-cache@12000000"};
    if(cache_dma_mib<64||cache_dma_mib>192||cache_dma_mib%4)return -EINVAL;
    if(test_backing&&!of_machine_is_compatible("linux,dummy-virt"))return -EPERM;
    for(unsigned int i=0;i<2;i++){
        bool use_dma=false;
        ag_u32 reserved_bytes=size[i];
        if(!test_backing){
            struct device_node *node=of_find_node_by_path(path[i]);
            struct reserved_mem *memory=node?of_reserved_mem_lookup(node):NULL;
            use_dma=i==1&&node&&of_device_is_compatible(node,"shared-dma-pool")&&of_property_read_bool(node,"reusable");
            bool valid=memory&&(i==0?ag_descriptor_range_valid(memory->base,memory->size):memory->base==base[i]&&memory->size==size[i])&&(use_dma||of_property_read_bool(node,"no-map"));
            if(valid)reserved_bytes=memory->size;
            of_node_put(node);if(!valid)return -ENODEV;
        }
        ag_u32 bytes=use_dma?cache_dma_mib*1048576U:reserved_bytes;
        if(ag_heap_init(&heaps[i],base[i],test_backing?1048576:bytes))return -EINVAL;
        if(test_backing){backing[i]=vmalloc_user(heaps[i].size);if(!backing[i])return -ENOMEM;}
        else if(use_dma){int ret=cache_dma_prepare();if(ret)return ret;}
        else {physical_backing[i]=i?ioremap_wc(base[i],bytes):ioremap(base[i],bytes);if(!physical_backing[i])return -ENOMEM;}
    }
    return 0;
}
static int __init av_init(void)
{
    struct netlink_kernel_cfg cfg={.input=ignore_input};
    int result;unsigned int devices=0;
    if(!enable)return -EPERM;
    result=prepare_heaps();if(result)goto free_backing;
    result=register_chrdev_region(MKDEV(246,0),2,"steamlink-av-shm");if(result)goto free_backing;
    result=register_chrdev_region(MKDEV(245,0),1,"steamlink-av-cc");if(result)goto unregister_shm;
    cdev_init(&shm_cdev,&shm_ops);shm_cdev.owner=THIS_MODULE;
    result=cdev_add(&shm_cdev,MKDEV(246,0),2);if(result)goto unregister_cc;
    cdev_init(&cc_cdev,&cc_ops);cc_cdev.owner=THIS_MODULE;
    result=cdev_add(&cc_cdev,MKDEV(245,0),1);if(result)goto delete_shm;
    av_class=class_create("steamlink-av");if(IS_ERR(av_class)){result=PTR_ERR(av_class);goto delete_cc;}
    av_class->devnode=node_mode;
    if(IS_ERR(device_create(av_class,NULL,MKDEV(246,0),NULL,"shm_cache"))){result=-ENOMEM;goto destroy_devices;}devices++;
    if(IS_ERR(device_create(av_class,NULL,MKDEV(246,1),NULL,"shm_noncache"))){result=-ENOMEM;goto destroy_devices;}devices++;
    if(IS_ERR(device_create(av_class,NULL,MKDEV(245,0),NULL,"galois_cc"))){result=-ENOMEM;goto destroy_devices;}devices++;
    cc_socket=netlink_kernel_create(&init_net,29,&cfg);if(!cc_socket){result=-EADDRINUSE;goto destroy_devices;}
    message_queue=alloc_workqueue("steamlink-cc",WQ_UNBOUND|WQ_MEM_RECLAIM,1);
    if(!message_queue){netlink_kernel_release(cc_socket);result=-ENOMEM;goto destroy_devices;}
    pr_info("steamlink-av: SHM/CC, %s backing; CBUF and validated noncached synchronization\n",test_backing?"VM CPU-only":"reserved WC physical");
    return 0;
destroy_devices:
    if(devices>2)device_destroy(av_class,MKDEV(245,0));
    if(devices>1)device_destroy(av_class,MKDEV(246,1));
    if(devices>0)device_destroy(av_class,MKDEV(246,0));
    class_destroy(av_class);
delete_cc:cdev_del(&cc_cdev);
delete_shm:cdev_del(&shm_cdev);
unregister_cc:unregister_chrdev_region(MKDEV(245,0),1);
unregister_shm:unregister_chrdev_region(MKDEV(246,0),2);
free_backing:cache_dma_cleanup();for(unsigned int i=0;i<2;i++){vfree(backing[i]);if(physical_backing[i])iounmap(physical_backing[i]);}return result;
}
static void __exit av_exit(void)
{
    destroy_workqueue(message_queue);
    netlink_kernel_release(cc_socket);
    device_destroy(av_class,MKDEV(245,0));device_destroy(av_class,MKDEV(246,0));device_destroy(av_class,MKDEV(246,1));
    class_destroy(av_class);cdev_del(&cc_cdev);cdev_del(&shm_cdev);
    unregister_chrdev_region(MKDEV(245,0),1);unregister_chrdev_region(MKDEV(246,0),2);
    cache_dma_cleanup();
    for(unsigned int i=0;i<2;i++){vfree(backing[i]);if(physical_backing[i])iounmap(physical_backing[i]);}
}
module_init(av_init);module_exit(av_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Experimental bounded ARM32 Steam Link SHM/CC foundations; no autoload");
