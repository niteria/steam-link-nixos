/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_PE_COMPAT_H
#define STEAMLINK_PE_COMPAT_H
#include <linux/io.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/delay.h>
#include <linux/timekeeping.h>
#include <linux/math64.h>
#define IRQF_DISABLED 0
void __iomem *slpe_mmio(unsigned long address);
int slpe_prepare(void);
void slpe_unmap(void);
bool slpe_is_faulted(void);
void slpe_fault(const char *reason);
static inline bool slpe_poll(unsigned int *budget)
{if(!*budget){slpe_fault("bounded DHUB/BCM poll exhausted");return false;}--*budget;udelay(1);return true;}
static inline u32 slpe_read32(unsigned long address){void __iomem *p=slpe_mmio(address);return p?readl(p):0;}
static inline void slpe_write32(unsigned long address,u32 value){void __iomem *p=slpe_mmio(address);if(p)writel(value,p);}
static inline u16 slpe_read16(unsigned long address){void __iomem *p=slpe_mmio(address);return p?readw(p):0;}
static inline void slpe_write16(unsigned long address,u16 value){void __iomem *p=slpe_mmio(address);if(p)writew(value,p);}
static inline u8 slpe_read8(unsigned long address){void __iomem *p=slpe_mmio(address);return p?readb(p):0;}
static inline void slpe_write8(unsigned long address,u8 value){void __iomem *p=slpe_mmio(address);if(p)writeb(value,p);}
int pe_agent_driver_init(void);
void pe_agent_driver_exit(void);
#endif
