/* SPDX-License-Identifier: GPL-2.0-only
 * Included after the vendor ISR/queue definitions. */
static void slpe_drain_tasks(void)
{
    struct tasklet_struct *tasks[]={&pe_vpp_tasklet,&pe_vpp_cec_tasklet,&pe_vdec_tasklet,
        &pe_vip_tasklet,&pe_ma_tasklet,&pe_sa_tasklet,&pe_spdif_tasklet,&pe_aip_tasklet,
        &pe_hdmi_tasklet,&pe_app_tasklet,&pe_zsp_tasklet,&pe_pg_done_tasklet,&pe_rle_err_tasklet,&pe_rle_done_tasklet};
    for(unsigned int i=0;i<ARRAY_SIZE(tasks);i++)tasklet_kill(tasks[i]);
}
static char *pe_mode(const struct device *dev,umode_t *mode){if(mode)*mode=0600;return NULL;}
static int __init pe_driver_init(void)
{
    struct device_node *node,*child;int result;
    result=slpe_prepare();if(result)return result;
    node=of_find_compatible_node(NULL,NULL,"mrvl,berlin-pe");
    if(!node){result=-ENODEV;goto unmap;}
    for(unsigned int i=0;i<=IRQ_DHUBINTRVPRO;i++){
        pe_irqs[i]=of_irq_get(node,i);
        if(pe_irqs[i]<=0){result=-EINVAL;of_node_put(node);goto unmap;}
    }
    for_each_child_of_node(node,child)if(of_device_is_compatible(child,"mrvl,berlin-cec")){
        pe_irqs[IRQ_SM_CEC]=of_irq_get(child,0);of_node_put(child);break;
    }
    of_node_put(node);
    if(pe_irqs[IRQ_SM_CEC]<=0){result=-EINVAL;goto unmap;}
    result=register_chrdev_region(MKDEV(244,0),2,"steamlink-pe");if(result)goto unmap;
    result=pe_device_init(0,&pe_dev);if(result)goto unregister;
    /* Stock FASTLOGO stops its interrupt sources before PE opens. The modern
     * kernel has no such owner: don't unmask a leftover level interrupt while
     * vpp_intr_status[] and the userspace service are still uninitialized. */
    void *semaphores[]={dhub_semaphore(&VPP_dhubHandle.dhub),dhub_semaphore(&AG_dhubHandle.dhub)};
    for(unsigned int h=0;h<ARRAY_SIZE(semaphores);h++)for(unsigned int bit=0;bit<32;bit++){
        semaphore_intr_enable(semaphores[h],bit,0,0,0,0,0);
        semaphore_clr_full(semaphores[h],bit);
    }
    if(slpe_is_faulted()){result=-EIO;goto stop_device;}
    cdev_init(&pe_dev,&pe_ops);pe_dev.owner=THIS_MODULE;
    result=cdev_add(&pe_dev,MKDEV(244,0),1);if(result)goto stop_device;
    pe_dev_class=class_create("steamlink-pe");
    if(IS_ERR(pe_dev_class)){result=PTR_ERR(pe_dev_class);goto delete;}
    pe_dev_class->devnode=pe_mode;
    if(IS_ERR(device_create(pe_dev_class,NULL,MKDEV(244,0),NULL,"galois_pe"))){result=-ENOMEM;goto destroy_class;}
    result=pe_agent_driver_init();if(result)goto destroy_node;
    pr_info("steamlink PE: pinned SDK control/IRQ/DHUB adapter loaded\n");return 0;
destroy_node:device_destroy(pe_dev_class,MKDEV(244,0));
destroy_class:class_destroy(pe_dev_class);
delete:cdev_del(&pe_dev);
stop_device:pe_device_exit(0,&pe_dev);
unregister:unregister_chrdev_region(MKDEV(244,0),2);
unmap:slpe_unmap();return result;
}
static void __exit pe_driver_exit(void)
{
    pe_agent_driver_exit();device_destroy(pe_dev_class,MKDEV(244,0));class_destroy(pe_dev_class);
    cdev_del(&pe_dev);pe_device_exit(0,&pe_dev);unregister_chrdev_region(MKDEV(244,0),2);slpe_unmap();
}
module_init(pe_driver_init);
module_exit(pe_driver_exit);
