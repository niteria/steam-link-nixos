#!/usr/bin/env python3
"""Apply checked modern-API/MMIO adaptations to the exact pinned GPL PE source."""
from pathlib import Path
import re
def replace(path, old, new):
    data=path.read_text();assert old in data,(path,old);path.write_text(data.replace(old,new))
driver=Path("pe_driver.c")
replace(driver,"#include <asm/uaccess.h>","#include <linux/uaccess.h>")
replace(driver,"DEFINE_SEMAPHORE(vpp_sem)","DEFINE_SEMAPHORE(vpp_sem, 0)")
replace(driver,"DEFINE_SEMAPHORE(vip_sem)","DEFINE_SEMAPHORE(vip_sem, 0)")
replace(driver,"class_create(THIS_MODULE, PE_DEVICE_NAME)","class_create(PE_DEVICE_NAME)")
# seq_file replacements are installed below after removing the obsolete readers.
data=driver.read_text();start=data.index("static int read_proc_status(");end=data.index("static int pe_driver_setup_cdev",start)
data=re.sub(r'DECLARE_TASKLET_DISABLED\((\w+),\s*(\w+),\s*0\)',r'DECLARE_TASKLET_DISABLED_OLD(\1, \2)',data)
# Locate after the signature rewrites, whose lengths differ from the original.
start=data.index("static int read_proc_status(");end=data.index("static int pe_driver_setup_cdev",start)
data=data[:start]+data[end:]
data=re.sub(r'\s*create_proc_read_entry\(PE_DEVICE_PROCFILE_(?:STATUS|DETAIL), 0, pe_driver_procdir,\s*read_proc_(?:status|detail), NULL\);',"",data)
# Drain legacy direct dereferences through mapped MMIO, not fixed kernel VAs.
data=data.replace("*((volatile int *)*(ptr + 1)) = *ptr;","slpe_write32(*(ptr + 1), *ptr);")
data=data.replace("{(*(volatile UINT*)(MEMMAP_TSI_REG_BASE+RA_DRMDMX_figoSys+RA_FigoSys_FIGO0+BA_FigoReg_figoIntrLvl_st)) = 1;}",
                  "{slpe_write32(MEMMAP_TSI_REG_BASE+RA_DRMDMX_figoSys+RA_FigoSys_FIGO0+BA_FigoReg_figoIntrLvl_st, 1);}")
data=data.replace("np = of_find_compatible_node(NULL, NULL, \"mrvl,berlin-pe\");",
                  "res = slpe_prepare(); if (res) return res;\n\tnp = of_find_compatible_node(NULL, NULL, \"mrvl,berlin-pe\");")
data=data.replace("if (!np)\n\t\treturn -ENODEV;","if (!np) { slpe_unmap(); return -ENODEV; }")
data=data.replace("pe_trace(\"pe_driver_init failed !!! (%d)\\n\", res);","slpe_unmap();\n\tpe_trace(\"pe_driver_init failed !!! (%d)\\n\", res);")
data=data.replace("pe_trace(\"pe_driver_exit OK\\n\");","slpe_unmap();\n\tpe_trace(\"pe_driver_exit OK\\n\");")
# Exclusive, balanced IRQ lifetime replaces the stock partial-open leak paths.
start=data.index("static int pe_driver_open(struct inode *inode, struct file *filp)\n{")
end=data.index("static long pe_driver_ioctl_unlocked",start)
data=data[:start]+'#include "open.h"\n\n'+data[end:]
data=data.replace("static int pe_irqs[IRQ_PE_MAX];","static int pe_irqs[IRQ_PE_MAX];\nstatic atomic_t slpe_vdec_enabled=ATOMIC_INIT(0);")
data=data.replace("disable_irq_nosync(irq);","if (atomic_cmpxchg(&slpe_vdec_enabled, 1, 0)) disable_irq_nosync(irq);")
data=data.replace("enable_irq(pe_irqs[IRQ_DHUBINTRVPRO]);","if (!atomic_cmpxchg(&slpe_vdec_enabled, 0, 1)) enable_irq(pe_irqs[IRQ_DHUBINTRVPRO]);")
data,n=re.subn(r'GA_REG_WORD32_READ\(0xf7e82C00 \+ 0x04 \+ 7 \* 0x14,\s*&(\w+)\);',
               r'\1 = slpe_read32(0xf7e82c90);',data)
assert n==4,n
data=data.replace("rx_buf.len = dptr_len;","if (dptr_len > sizeof(rx_buf.buf)) { slpe_fault(\"CEC receive size\"); return IRQ_HANDLED; }\n\t\t\trx_buf.len = dptr_len;")
data,n=re.subn(r'while \(!BCM_SCHED_PushCmd\(([^\n]+)\)\)',r'for (unsigned int retries=10000; !BCM_SCHED_PushCmd(\1) && slpe_poll(&retries); )',data)
assert n==6,n
# Drain tasklets before freeing their queues, and handle initialization failures.
data=data.replace("\t\ttasklet_disable(","\t\ttasklet_kill(").replace("\ttasklet_disable(","\ttasklet_kill(")
start=data.index("static int pe_device_exit(");end=data.index("/*******************************************************************************",start)
data=data[:start]+'''static int pe_device_exit(unsigned int cpu_id, void *pHandle)
{
    unregister_reboot_notifier(&pe_reboot_notifier);
    slpe_drain_tasks();
    PEMsgQ_Destroy(&hPEVIPMsgQ);PEMsgQ_Destroy(&hPEMsgQ);return 0;
}

'''+data[end:]
data=data.replace("static int pe_device_exit(","static int pe_device_exit(",1)
data=data.replace("static int pe_device_init(unsigned int cpu_id, void *pHandle)","static void slpe_drain_tasks(void);\nstatic int pe_device_init(unsigned int cpu_id, void *pHandle)")
start=data.index("static int __init pe_driver_init(");end=data.index("MODULE_AUTHOR",start)
data=data[:start]+'#include "lifecycle.h"\n\n'+data[end:]
# Validate array indices before the vendor code dereferences them.
data=data.replace("\t\tdma_info[user_dma_info.cpcbID].DmaAddr =", "\t\tif (user_dma_info.cpcbID >= 3 || user_dma_info.DmaAddr >= 0xc000000 || user_dma_info.DmaLen > (0xc000000-user_dma_info.DmaAddr)/8) return -EINVAL;\n\t\tdma_info[user_dma_info.cpcbID].DmaAddr =")
for name in ("vbi_bcm_info","vde_bcm_info"):
    data=data.replace(f"\t\t{name}[user_dma_info.cpcbID].DmaAddr =",f"\t\tif (user_dma_info.cpcbID >= 3 || user_dma_info.DmaAddr >= 0xc000000 || user_dma_info.DmaLen > (0xc000000-user_dma_info.DmaAddr)/8) return -EINVAL;\n\t\t{name}[user_dma_info.cpcbID].DmaAddr =")
# Propagate a latched MMIO/poll fault instead of reporting a completed command.
data=data.replace("\tVPP_DMA_INFO user_dma_info;","\tif (slpe_is_faulted()) return -EIO;\n\tVPP_DMA_INFO user_dma_info;")
data=data.replace("\treturn 0;\n}\n\nstatic int pe_driver_setup_cdev", "\treturn slpe_is_faulted() ? -EIO : 0;\n}\n\nstatic int pe_driver_setup_cdev")
driver.write_text(data)
io=Path("sdk/modules/pe/gsinc/galois_io.h")
data=io.read_text()
for bits,ctype,read,write in (("32","unsigned int","readl","writel"),("16","unsigned short","readw","writew"),("08","unsigned char","readb","writeb")):
    data=re.sub(r"#define\s+MV_MEMIO"+bits+r"_WRITE\(addr, data\).*",f"#define MV_MEMIO{bits}_WRITE(addr, data) slpe_write{int(bits)}((addr), (data))",data)
    data=re.sub(r"#define\s+MV_MEMIO"+bits+r"_READ\(addr\).*",f"#define MV_MEMIO{bits}_READ(addr) slpe_read{int(bits)}(addr)",data)
io.write_text(data)
hal=Path("hal_dhub.c")
replace(hal,"(d) = *(UNSG32*)(a);","(d) = slpe_read32(a);")
replace(hal,"*(UNSG32*)(a) = (d);","slpe_write32(a, d);")
replace(hal,"((*((volatile unsigned int*)(addr))) = ((unsigned int)(data)))","slpe_write32(addr, data)")
replace(hal,"((*((volatile unsigned int*)(addr))))","slpe_read32(addr)")
data=hal.read_text()
data,n=re.subn(r'UNSG32 d;\n\s*do\s*\{', 'UNSG32 d;\n\tunsigned int retries=10000;\n\tdo {',data)
assert n==3,n
assert data.count('} while(d);')==3
data=data.replace('} while(d);','} while(d && slpe_poll(&retries));')
hal.write_text(data)
