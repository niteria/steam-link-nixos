/* SPDX-License-Identifier: GPL-2.0-only
 * Fixed-width form of Valve SDK 62b4d09's ARM32 SHM/CC wire contracts. */
#ifndef SL_AV_UAPI_H
#define SL_AV_UAPI_H
#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 ag_u8;
typedef u32 ag_u32;
typedef s32 ag_s32;
#else
#include <stdint.h>
typedef uint8_t ag_u8;
typedef uint32_t ag_u32;
typedef int32_t ag_s32;
#endif
#define AG_BAD 0xffffffffU
#define AG_DYNAMIC 0x80000000U
#define AG_SHM_INFO 0x1f01
#define AG_SHM_BASE 0x1f02
#define AG_SHM_ALLOC 0x1f11
#define AG_SHM_FREE 0x1f12
#define AG_SHM_INVALIDATE 0x1f21
#define AG_SHM_CLEAN 0x1f22
#define AG_SHM_FLUSH 0x1f23
/* _IOWR('S', 0x40, struct ag_dmabuf), fixed ARM32 layout. */
#define AG_SHM_EXPORT 0xc0185340U
#define AG_CC_REG 0x1e01
#define AG_CC_FREE 0x1e02
#define AG_CC_INQUIRY 0x1e03
#define AG_CC_LIST 0x1e04
#define AG_CC_STATUS 0x1e05
#define AG_CC_TEST_MSG 0x1e06
#define AG_CC_UPDATE 0x1e07
#define AG_CC_CREATE_CBUF 0x1e08
#define AG_CC_DESTROY_CBUF 0x1e09
#define AG_E_BADVALUE ((ag_s32)0x80007060U)
#define AG_E_FULL ((ag_s32)0x80007063U)
#define AG_E_EXISTS ((ag_s32)0x80007066U)
#define AG_E_MISSING ((ag_s32)0x80007067U)
#define AG_E_ACCESS ((ag_s32)0x80007005U)
struct ag_operation { ag_u32 p1,p2,p3; };
struct ag_dmabuf { ag_u32 offset,length; ag_s32 fd; ag_u32 plane_offset,allocation_size,flags; };
struct ag_meminfo { ag_u32 total,used,free,peak,max_free,min_free,max_used,min_used,nfree,nused; };
struct ag_baseinfo { ag_u32 size,threshold,physical,virtual_address; ag_s32 fd; };
struct ag_service { ag_u32 id; ag_u8 level,type,pad1,pad2,data[24]; };
struct ag_list { ag_u32 capacity,count,total; struct ag_service entries[]; };
struct ag_status { ag_u32 reg,reg_error,freed,free_error,inquiry,inquiry_error,update,update_error,count,last,sequence; };
struct ag_message { ag_u32 id,param1,param2; };
struct ag_cbuf_service { ag_u32 id; ag_u8 level,type,pad1,pad2; ag_u32 refs,offset,size,end_size,flags,notice; };
struct ag_cbuf_body { ag_u32 start,read,write,size,end_size,flags; };
_Static_assert(sizeof(struct ag_operation)==12,"ARM32 operation ABI");
_Static_assert(sizeof(struct ag_dmabuf)==24,"DMA-BUF export ABI");
_Static_assert(sizeof(struct ag_meminfo)==40,"ARM32 meminfo ABI");
_Static_assert(sizeof(struct ag_baseinfo)==20,"ARM32 baseinfo ABI");
_Static_assert(sizeof(struct ag_service)==32,"CC service ABI");
_Static_assert(sizeof(struct ag_list)==12,"CC list ABI");
_Static_assert(sizeof(struct ag_status)==44,"CC status ABI");
_Static_assert(sizeof(struct ag_message)==12,"CC message ABI");
_Static_assert(sizeof(struct ag_cbuf_service)==32,"CC CBUF service ABI");
_Static_assert(sizeof(struct ag_cbuf_body)==24,"CC CBUF body ABI");
#endif
