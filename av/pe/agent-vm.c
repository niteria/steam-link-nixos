// SPDX-License-Identifier: GPL-2.0-only
/* VM harness for the same agent.c linked into the physical PE module. */
#include "compat.h"
#include <linux/fs.h>
static int __init agent_vm_init(void)
{
    int result;
    if(!of_machine_is_compatible("linux,dummy-virt"))return -EPERM;
    result=register_chrdev_region(MKDEV(244,0),2,"steamlink-agent-vm");if(result)return result;
    result=pe_agent_driver_init();if(result)unregister_chrdev_region(MKDEV(244,0),2);return result;
}
static void __exit agent_vm_exit(void){pe_agent_driver_exit();unregister_chrdev_region(MKDEV(244,0),2);}
module_init(agent_vm_init);module_exit(agent_vm_exit);MODULE_LICENSE("GPL");
