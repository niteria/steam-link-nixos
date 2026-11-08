// SPDX-License-Identifier: GPL-2.0-only
/* Legacy ARM32 TWSI0 ioctls over the modern I2C controller's transfer API. */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "twsi-overlay-data.h"
_Static_assert(__alignof__(twsi_overlay)>=8,"embedded TWSI DTB must satisfy libfdt alignment");
struct twsi_rw { u32 master,address,type,read_count,read_buffer,write_count,write_buffer; };
struct twsi_speed { u32 master,type,khz; };
_Static_assert(sizeof(struct twsi_rw)==28,"ARM32 TWSI read/write ABI");
static bool enable,test_backing,activate_bus;
module_param(enable,bool,0400);module_param(test_backing,bool,0400);
module_param(activate_bus,bool,0400);
static int overlay_id=-1;
static int test_error;
module_param(test_error,int,0600);
static struct i2c_adapter *adapter;
static struct i2c_adapter model_adapter;
static struct class *twsi_class;
static struct cdev twsi_cdev;
static dev_t twsi_number;
static u32 frequency;
static int model_transfer(struct i2c_adapter *adap,struct i2c_msg *messages,int count)
{
    unsigned int reg=0;
    if(test_error)return test_error<0?test_error:-EIO;
    for(int i=0;i<count;i++){
        struct i2c_msg *m=&messages[i];
        if(m->flags&I2C_M_RD)for(unsigned int j=0;j<m->len;j++)m->buf[j]=(u8)(reg+j)^m->addr;
        else if(m->len)reg=m->buf[0];
    }
    return count;
}
static u32 model_functionality(struct i2c_adapter *adap){return I2C_FUNC_I2C|I2C_FUNC_10BIT_ADDR;}
static const struct i2c_algorithm model_algorithm={.master_xfer=model_transfer,.functionality=model_functionality};
static long twsi_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
    struct twsi_rw request;
    struct i2c_msg messages[2];unsigned int count=0;int result;
    u8 *write_buffer=NULL,*read_buffer=NULL;
    if(cmd==0x1234){
        struct twsi_speed speed;
        if(copy_from_user(&speed,(void __user *)arg,sizeof(speed)))return -EFAULT;
        if(speed.master||speed.type>1)return -EINVAL;
        /* No generic I2C API can safely retune another driver's controller.
         * The profile DT fixes 100/400kHz; reject unsupported rates explicitly. */
        return !speed.khz||speed.khz==frequency/1000?0:-EOPNOTSUPP;
    }
    if(cmd!=0x1619)return -ENOTTY;
    if(copy_from_user(&request,(void __user *)arg,sizeof(request)))return -EFAULT;
    if(request.master||request.type>1||request.address>(request.type?0x3ff:0x7f)||
       request.read_count>4096||request.write_count>4096||(!request.read_count&&!request.write_count))return -EINVAL;
    if(request.type&&!i2c_check_functionality(adapter,I2C_FUNC_10BIT_ADDR))return -EOPNOTSUPP;
    if(request.write_count){
        write_buffer=memdup_user((void __user *)(unsigned long)request.write_buffer,request.write_count);
        if(IS_ERR(write_buffer))return PTR_ERR(write_buffer);
        messages[count++]=(struct i2c_msg){.addr=request.address,.flags=request.type?I2C_M_TEN:0,
                                        .len=request.write_count,.buf=write_buffer};
    }
    if(request.read_count){
        read_buffer=kmalloc(request.read_count,GFP_KERNEL);if(!read_buffer){result=-ENOMEM;goto out;}
        messages[count++]=(struct i2c_msg){.addr=request.address,.flags=I2C_M_RD|(request.type?I2C_M_TEN:0),
                                        .len=request.read_count,.buf=read_buffer};
    }
    result=i2c_transfer(adapter,messages,count);
    if(result==count){result=0;if(request.read_count&&copy_to_user((void __user *)(unsigned long)request.read_buffer,read_buffer,request.read_count))result=-EFAULT;}
    else if(result>=0)result=-EIO;
out:kfree(read_buffer);kfree(write_buffer);return result;
}
static const struct file_operations twsi_ops={.owner=THIS_MODULE,.open=nonseekable_open,.unlocked_ioctl=twsi_ioctl};
static char *twsi_mode(const struct device *dev,umode_t *mode){if(mode)*mode=0600;return NULL;}
static int __init twsi_init(void)
{
    int result;
    if(!enable)return -EPERM;
    if(test_backing){
        if(!of_machine_is_compatible("linux,dummy-virt"))return -EPERM;
        model_adapter.owner=THIS_MODULE;model_adapter.algo=&model_algorithm;
        strscpy(model_adapter.name,"steamlink VM-only TWSI model",sizeof(model_adapter.name));
        result=i2c_add_adapter(&model_adapter);if(result)return result;
        adapter=&model_adapter;frequency=400000;
    }else{
        struct device_node *node;
        if(!of_machine_is_compatible("valve,steamlink"))return -EPERM;
        if(activate_bus){
            pr_info("steamlink twsi: overlay alignment %lu, magic %02x%02x%02x%02x, bytes %u\n",
                    (unsigned long)twsi_overlay&7,twsi_overlay[0],twsi_overlay[1],twsi_overlay[2],twsi_overlay[3],twsi_overlay_len);
            result=of_overlay_fdt_apply(twsi_overlay,twsi_overlay_len,&overlay_id,NULL);
            if(result){pr_err("steamlink twsi: overlay activation failed: %d\n",result);return result;}
        }
        node=of_find_node_by_path("/soc@f7000000/apb@e80000/i2c@1400");
        if(!node){result=-ENODEV;goto remove_overlay;}
        result=of_property_read_u32(node,"clock-frequency",&frequency);
        pr_info("steamlink twsi: bus frequency %u, property status %d\n",frequency,result);
        adapter=result?NULL:of_get_i2c_adapter_by_node(node);of_node_put(node);
        if(!adapter){result=-ENODEV;goto remove_overlay;}
        if(frequency!=100000&&frequency!=400000){result=-EINVAL;goto put;}
    }
    result=alloc_chrdev_region(&twsi_number,0,1,"steamlink-twsi");if(result){pr_err("steamlink twsi: chrdev allocation: %d\n",result);goto put;}
    pr_info("steamlink twsi: device %u:%u\n",MAJOR(twsi_number),MINOR(twsi_number));
    cdev_init(&twsi_cdev,&twsi_ops);twsi_cdev.owner=THIS_MODULE;
    result=cdev_add(&twsi_cdev,twsi_number,1);if(result){pr_err("steamlink twsi: cdev registration: %d\n",result);goto unregister;}
    twsi_class=class_create("steamlink-twsi");
    if(IS_ERR(twsi_class)){result=PTR_ERR(twsi_class);goto delete;}
    twsi_class->devnode=twsi_mode;
    if(IS_ERR(device_create(twsi_class,NULL,twsi_number,NULL,"twsi0"))){result=-ENOMEM;class_destroy(twsi_class);goto delete;}
    return 0;
delete:cdev_del(&twsi_cdev);
unregister:unregister_chrdev_region(twsi_number,1);
put:if(test_backing)i2c_del_adapter(&model_adapter);else i2c_put_adapter(adapter);
remove_overlay:if(overlay_id>=0)of_overlay_remove(&overlay_id);return result;
}
static void __exit twsi_exit(void)
{
    device_destroy(twsi_class,twsi_number);class_destroy(twsi_class);cdev_del(&twsi_cdev);
    unregister_chrdev_region(twsi_number,1);
    if(test_backing)i2c_del_adapter(&model_adapter);else i2c_put_adapter(adapter);
    if(overlay_id>=0)of_overlay_remove(&overlay_id);
}
module_init(twsi_init);module_exit(twsi_exit);
MODULE_LICENSE("GPL");MODULE_DESCRIPTION("Steam Link legacy TWSI0 ABI over modern I2C transfers");
