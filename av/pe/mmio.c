// SPDX-License-Identifier: GPL-2.0-only
#include "compat.h"
#include <linux/ioport.h>
#include <linux/clk.h>
#include "av-memory-layout.h"
static struct clk *av_clocks[18];
static unsigned int clock_count;
static bool enable;
static bool faulted;
module_param(enable,bool,0400);
module_param(faulted,bool,0400);
bool slpe_is_faulted(void){return READ_ONCE(faulted);}
void slpe_fault(const char *reason){if(!xchg(&faulted,true))pr_err("steamlink PE fault latched: %s\n",reason);}
/* Separate resources, never a blanket mapping of modern SoC controllers. */
static struct window { resource_size_t base,size; void __iomem *mapped; struct resource *claimed; } windows[]={
    {0xf7ae0000,0x20000},{0xf7b50000,0x10000},{0xf7c00000,0x80000},
    {0xf7d00000,0x20000},{0xf7f40000,0x40000},{0xf7fe1000,0x1000},
    /* DT timer7 is disabled/unclaimed. Share its stock counter with vendor
     * userspace; no blanket APB timer mapping and no timer reprogramming. */
    {0xf7e82c8c,0x14},
};
void __iomem *slpe_mmio(unsigned long address)
{
    if(slpe_is_faulted())return NULL;
    for(unsigned int i=0;i<ARRAY_SIZE(windows);i++){
        struct window *w=&windows[i];
        if(address>=w->base&&address-w->base<=w->size-4&&w->mapped)return w->mapped+(address-w->base);
    }
    pr_err_ratelimited("steamlink PE: rejected MMIO address %08lx\n",address);slpe_fault("MMIO outside claimed windows");return NULL;
}
void slpe_unmap(void)
{
    for(unsigned int i=0;i<ARRAY_SIZE(windows);i++){
        struct window *w=&windows[i];
        if(w->mapped){iounmap(w->mapped);w->mapped=NULL;}
        if(w->claimed){release_mem_region(w->base,w->size);w->claimed=NULL;}
    }
    while(clock_count){struct clk *clk=av_clocks[--clock_count];clk_disable_unprepare(clk);clk_put(clk);}
}
int slpe_prepare(void)
{
    const char *paths[]={"/reserved-memory/av-cache@12000000","/reserved-memory/av-noncache@1e000000"};
    const u32 bases[]={0x12000000,0x1e000000},sizes[]={0xc000000,0x2000000};
    if(!enable || !of_machine_is_compatible("valve,steamlink"))return -EPERM;
    struct device_node *low=of_find_node_by_path("/reserved-memory/av-low@0");
    u32 low_range[2];
    bool low_valid=low&&!of_property_read_u32_array(low,"reg",low_range,2)&&low_range[0]==0&&low_range[1]==0x1000000&&of_property_read_bool(low,"no-map");
    of_node_put(low);if(!low_valid)return -ENODEV;
    for(unsigned int i=0;i<2;i++){
        struct device_node *node=of_find_node_by_path(paths[i]);
        struct reserved_mem *memory=node?of_reserved_mem_lookup(node):NULL;
        bool dma_cache=i==0&&node&&of_device_is_compatible(node,"shared-dma-pool")&&of_property_read_bool(node,"reusable");
        bool valid=memory&&(i==1?ag_descriptor_range_valid(memory->base,memory->size):memory->base==bases[i]&&memory->size==sizes[i])&&(dma_cache||of_property_read_bool(node,"no-map"));
        of_node_put(node);if(!valid)return -ENODEV;
    }
    struct device_node *pe=of_find_compatible_node(NULL,NULL,"mrvl,berlin-pe");
    if(!pe)return -ENODEV;
    int count=of_count_phandle_with_args(pe,"clocks","#clock-cells");
    if(count!=(int)ARRAY_SIZE(av_clocks)){of_node_put(pe);return -EINVAL;}
    for(int i=0;i<count;i++){
        struct clk *clk=of_clk_get(pe,i);int result;
        if(IS_ERR(clk)){result=PTR_ERR(clk);of_node_put(pe);slpe_unmap();return result;}
        result=clk_prepare_enable(clk);
        if(result){clk_put(clk);of_node_put(pe);slpe_unmap();return result;}
        av_clocks[clock_count++]=clk;
        pr_info("steamlink PE: clock %d enabled at %lu Hz\n",i,clk_get_rate(clk));
    }
    of_node_put(pe);
    for(unsigned int i=0;i<ARRAY_SIZE(windows);i++){
        struct window *w=&windows[i];
        w->claimed=request_mem_region(w->base,w->size,"steamlink-pe");
        if(!w->claimed){slpe_unmap();return -EBUSY;}
        w->mapped=ioremap(w->base,w->size);
        if(!w->mapped){slpe_unmap();return -ENOMEM;}
    }
    return 0;
}
