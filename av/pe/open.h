/* SPDX-License-Identifier: GPL-2.0-only */
static atomic_t pe_opened=ATOMIC_INIT(0);
#include "irq-rate.h"
static struct slpe_irq_rate irq_rate[IRQ_PE_MAX];
static void slpe_drain_tasks(void);
static irqreturn_t pe_irq_dispatch(int irq,void *data)
{
    irq_handler_t handlers[IRQ_PE_MAX]={[IRQ_ZSPINT]=pe_devices_zsp_isr,
        [IRQ_DHUBINTRAVIO0]=pe_devices_vpp_isr,[IRQ_DHUBINTRAVIO1]=pe_devices_aout_isr,
        [IRQ_DHUBINTRAVIO2]=pe_devices_vip_isr,[IRQ_DHUBINTRVPRO]=pe_devices_vdec_isr,
        [IRQ_SM_CEC]=pe_devices_vpp_cec_isr};
    irqreturn_t result=IRQ_NONE;
    for(unsigned int i=0;i<IRQ_PE_MAX;i++)if(pe_irqs[i]==irq){
        if(slpe_irq_rate_exceeded(&irq_rate[i],jiffies,HZ)){
            pr_err("steamlink PE: IRQ storm on %d (source %u), VPP full=%08x AG full=%08x, disabled\n",
                irq,i,semaphore_chk_full(dhub_semaphore(&VPP_dhubHandle.dhub),-1),
                semaphore_chk_full(dhub_semaphore(&AG_dhubHandle.dhub),-1));
            slpe_fault("IRQ storm");
        }
        if(!slpe_is_faulted())result=handlers[i](irq,data);
        if(slpe_is_faulted()){
            if(i!=IRQ_DHUBINTRVPRO||atomic_cmpxchg(&slpe_vdec_enabled,1,0))disable_irq_nosync(irq);
            result=IRQ_HANDLED;
        }
        break;
    }
    return result;
}
static int pe_driver_open(struct inode *inode,struct file *file)
{
    unsigned int i;int result;
    if(slpe_is_faulted())return -EIO;
    if(atomic_cmpxchg(&pe_opened,0,1))return -EBUSY;
    for(i=0;i<IRQ_PE_MAX;i++){
        unsigned long flags=i==IRQ_DHUBINTRVPRO?IRQF_NO_AUTOEN:0;
        result=request_irq(pe_irqs[i],pe_irq_dispatch,flags,"steamlink-pe",&pe_dev);
        if(result)goto unwind;
    }
    return nonseekable_open(inode,file);
unwind:
    while(i)free_irq(pe_irqs[--i],&pe_dev);
    slpe_drain_tasks();atomic_set(&pe_opened,0);return result;
}
static int pe_driver_release(struct inode *inode,struct file *file)
{
    for(unsigned int i=0;i<IRQ_PE_MAX;i++)free_irq(pe_irqs[i],&pe_dev);
    slpe_drain_tasks();atomic_set(&slpe_vdec_enabled,0);atomic_set(&pe_opened,0);return 0;
}
