/* SPDX-License-Identifier: GPL-2.0-only
 * Shared, externally locked metadata engine used by the actual kernel module. */
#ifndef SL_AV_CORE_H
#define SL_AV_CORE_H
#include "uapi.h"
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
#define AG_BLOCKS 512U
#define AG_SERVICES 100U
struct ag_block { ag_u32 offset,length,owner; };
struct ag_heap { ag_u32 base,size,count,used,peak; struct ag_block blocks[AG_BLOCKS]; };
struct ag_entry { struct ag_service value; ag_u32 owner; };
struct ag_registry { struct ag_status status; struct ag_entry entries[AG_SERVICES]; };
static inline int ag_heap_init(struct ag_heap *h,ag_u32 base,ag_u32 size)
{
    if(!size || size>AG_BAD-base || (base&31) || (size&31)) return -1;
    memset(h,0,sizeof(*h));h->base=base;h->size=size;return 0;
}
static inline ag_u32 ag_allocate(struct ag_heap *h,ag_u32 owner,ag_u32 size,ag_u32 alignment)
{
    ag_u32 cursor=0,index=0,offset;
    if(!owner || h->count==AG_BLOCKS || size>h->size || size>AG_BAD-31) return AG_BAD;
    if(alignment && ((alignment&(alignment-1)) || alignment>0x400000)) return AG_BAD;
    if(alignment<32) alignment=32;
    size=size?((size+31)&~31U):32;
    for(index=0;index<=h->count;index++) {
        ag_u32 limit=index<h->count?h->blocks[index].offset:h->size;
        if(h->base+cursor>AG_BAD-(alignment-1)) return AG_BAD;
        offset=((h->base+cursor+alignment-1)&~(alignment-1))-h->base;
        if(offset<=limit && size<=limit-offset) break;
        if(index<h->count) cursor=h->blocks[index].offset+h->blocks[index].length;
    }
    if(index>h->count) return AG_BAD;
    memmove(h->blocks+index+1,h->blocks+index,(h->count-index)*sizeof(*h->blocks));
    h->blocks[index]=(struct ag_block){offset,size,owner};h->count++;h->used+=size;
    if(h->used>h->peak) h->peak=h->used;
    return offset;
}
static inline int ag_free(struct ag_heap *h,ag_u32 owner,ag_u32 offset)
{
    for(ag_u32 i=0;i<h->count;i++) if(h->blocks[i].offset==offset) {
        if(h->blocks[i].owner!=owner) return -1;
        h->used-=h->blocks[i].length;
        memmove(h->blocks+i,h->blocks+i+1,(h->count-i-1)*sizeof(*h->blocks));h->count--;return 0;
    }
    return -1;
}
static inline void ag_heap_release(struct ag_heap *h,ag_u32 owner)
{ for(ag_u32 i=0;i<h->count;) { if(h->blocks[i].owner==owner) ag_free(h,owner,h->blocks[i].offset);else i++; } }
static inline void ag_heap_info(const struct ag_heap *h,struct ag_meminfo *m)
{
    ag_u32 cursor=0;memset(m,0,sizeof(*m));m->total=h->size;m->used=h->used;m->free=h->size-h->used;m->peak=h->peak;m->nused=h->count;
    for(ag_u32 i=0;i<=h->count;i++) {
        ag_u32 end=i<h->count?h->blocks[i].offset:h->size,gap=end-cursor;
        if(gap) { if(!m->nfree || gap<m->min_free)m->min_free=gap;if(gap>m->max_free)m->max_free=gap;m->nfree++; }
        if(i<h->count) { ag_u32 n=h->blocks[i].length;if(!i||n<m->min_used)m->min_used=n;if(n>m->max_used)m->max_used=n;cursor=end+n; }
    }
}
static inline int ag_map_valid(const struct ag_heap *h,ag_u32 offset,ag_u32 length)
{ return length && !(offset&4095) && !(length&4095) && offset<=h->size && length<=h->size-offset; }
static inline int ag_find_service(const struct ag_registry *r,ag_u32 id)
{ for(ag_u32 i=0;i<r->status.count;i++)if(r->entries[i].value.id==id)return (int)i;return -1; }
static inline ag_s32 ag_register(struct ag_registry *r,ag_u32 owner,struct ag_service *s)
{
    if(!owner||!s->id||s->id==0x7fffffffU) return AG_E_BADVALUE;
    if(r->status.count==AG_SERVICES){r->status.reg_error++;return AG_E_FULL;}
    if(s->id==AG_DYNAMIC) {
        do { if(r->status.sequence==0x7fffffffU)return AG_E_FULL;s->id=AG_DYNAMIC|++r->status.sequence; }while(ag_find_service(r,s->id)>=0);
    }
    if(ag_find_service(r,s->id)>=0){r->status.reg_error++;return AG_E_EXISTS;}
    ag_u32 i=0;while(i<r->status.count&&r->entries[i].value.id<s->id)i++;
    memmove(r->entries+i+1,r->entries+i,(r->status.count-i)*sizeof(*r->entries));
    r->entries[i]=(struct ag_entry){*s,owner};r->status.count++;r->status.reg++;r->status.last=s->id;return 0;
}
static inline ag_s32 ag_unregister(struct ag_registry *r,ag_u32 owner,ag_u32 id)
{
    int i=ag_find_service(r,id);
    if(i<0){r->status.free_error++;return AG_E_MISSING;}
    if(r->entries[i].owner!=owner){r->status.free_error++;return AG_E_ACCESS;}
    memmove(r->entries+i,r->entries+i+1,(r->status.count-(ag_u32)i-1)*sizeof(*r->entries));
    r->status.count--;r->status.freed++;r->status.last=id;return 0;
}
static inline ag_s32 ag_inquire(struct ag_registry *r,struct ag_service *s)
{
    int i=ag_find_service(r,s->id);
    if(i<0){r->status.inquiry_error++;return AG_E_MISSING;}
    *s=r->entries[i].value;r->status.inquiry++;r->status.last=s->id;return 0;
}
static inline ag_s32 ag_update(struct ag_registry *r,ag_u32 owner,const struct ag_service *s)
{
    int i=ag_find_service(r,s->id);
    if(i<0){r->status.update_error++;return AG_E_MISSING;}
    if(r->entries[i].owner!=owner){r->status.update_error++;return AG_E_ACCESS;}
    r->entries[i].value=*s;r->status.update++;r->status.last=s->id;return 0;
}
static inline void ag_registry_release(struct ag_registry *r,ag_u32 owner)
{ for(ag_u32 i=0;i<r->status.count;) { if(r->entries[i].owner==owner)ag_unregister(r,owner,r->entries[i].value.id);else i++; } }
#endif
