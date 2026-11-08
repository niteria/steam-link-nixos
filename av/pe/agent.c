// SPDX-License-Identifier: GPL-2.0-only
/* Stock ARM32 token/handle wire protocol, with file lifetime and bounded slots. */
#include "compat.h"
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
extern int MV_CC_MsgQ_PostMsgByID(u32 service,void *message);
struct agent { u32 token,handle,tgid; };
static struct agent *agents[64];
static DEFINE_MUTEX(agent_lock);
static struct cdev agent_cdev;
static struct class *agent_class;
static int agent_open(struct inode *inode,struct file *file)
{
    struct agent *a=kzalloc(sizeof(*a),GFP_KERNEL);unsigned int i;
    if(!a)return -ENOMEM;
    a->tgid=task_tgid_nr(current);
    mutex_lock(&agent_lock);
    for(i=0;i<ARRAY_SIZE(agents);i++)if(!agents[i]){agents[i]=a;break;}
    mutex_unlock(&agent_lock);
    if(i==ARRAY_SIZE(agents)){kfree(a);return -EMFILE;}
    file->private_data=a;return nonseekable_open(inode,file);
}
static int agent_release(struct inode *inode,struct file *file)
{
    struct agent *a=file->private_data;
    u32 message[]={0x80000001,a->handle,a->tgid};
    mutex_lock(&agent_lock);
    for(unsigned int i=0;i<ARRAY_SIZE(agents);i++)if(agents[i]==a){agents[i]=NULL;break;}
    mutex_unlock(&agent_lock);
    MV_CC_MsgQ_PostMsgByID(0x60011,message);kfree(a);return 0;
}
static long agent_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
    struct agent *a=file->private_data;long result=-ENOTTY;
    u32 message[3];
    if(cmd==0xbeef5002&&copy_from_user(message,(void __user *)arg,sizeof(message)))return -EFAULT;
    mutex_lock(&agent_lock);
    if(cmd==0xbeef5001){
        result=arg?0:-EINVAL;
        for(unsigned int i=0;!result&&i<ARRAY_SIZE(agents);i++)
            if(agents[i]&&agents[i]!=a&&agents[i]->token==arg)result=-EEXIST;
        if(!result)a->token=arg;
    }else if(cmd==0xbeef5002){
        result=-ENODEV;
        for(unsigned int i=0;message[0]&&i<ARRAY_SIZE(agents);i++)
            if(agents[i]&&agents[i]->token==message[0]){agents[i]->handle=message[1];result=0;break;}
    }
    mutex_unlock(&agent_lock);return result;
}
static const struct file_operations agent_ops={.owner=THIS_MODULE,.open=agent_open,.release=agent_release,.unlocked_ioctl=agent_ioctl};
static char *agent_mode(const struct device *dev,umode_t *mode){if(mode)*mode=0600;return NULL;}
int pe_agent_driver_init(void)
{
    int result;cdev_init(&agent_cdev,&agent_ops);agent_cdev.owner=THIS_MODULE;
    result=cdev_add(&agent_cdev,MKDEV(244,1),1);if(result)return result;
    agent_class=class_create("steamlink-pe-agent");
    if(IS_ERR(agent_class)){result=PTR_ERR(agent_class);goto delete;}
    agent_class->devnode=agent_mode;
    if(IS_ERR(device_create(agent_class,NULL,MKDEV(244,1),NULL,"galois_pe_agent"))){result=-ENOMEM;class_destroy(agent_class);goto delete;}
    return 0;
delete:cdev_del(&agent_cdev);return result;
}
void pe_agent_driver_exit(void)
{device_destroy(agent_class,MKDEV(244,1));class_destroy(agent_class);cdev_del(&agent_cdev);}
