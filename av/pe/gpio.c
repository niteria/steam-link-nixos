// SPDX-License-Identifier: GPL-2.0-only
/* Stock data-mode GPIO ABI, using exclusive modern GPIO consumers. */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
struct legacy_gpio {s32 port,mode;u32 data;};
struct legacy_regs {u32 data,direction,control; s32 modes[32];};
_Static_assert(sizeof(struct legacy_gpio)==12,"stock GPIO ARM32 ABI");
_Static_assert(sizeof(struct legacy_regs)==140,"stock GPIO snapshot ABI");
static bool enable,test_backing;
module_param(enable,bool,0400);module_param(test_backing,bool,0400);
static struct {struct gpio_device *device;struct gpio_chip *chip;} banks[6];
static struct gpio_desc *owned[48];
static int modes[48],values[48];
static DEFINE_MUTEX(gpio_lock);
struct find_bank {struct device_node *node;struct gpio_chip *chip;};
static int match_bank(struct gpio_chip *chip,const void *data)
{
    struct find_bank *find=(struct find_bank *)data;
    if(chip->parent&&chip->parent->of_node==find->node){find->chip=chip;return 1;}
    return 0;
}
static int port_index(bool sm,int port)
{return port>=0&&port<(sm?16:32)?port+(sm?32:0):-EINVAL;}
static struct gpio_desc *descriptor(unsigned int index)
{return gpio_device_get_desc(banks[index/8].device,index%8);}
static int set_mode(unsigned int index,int mode)
{
    int result;
    if(mode!=1&&mode!=2)return -EOPNOTSUPP;
    /* The restart line is not an AV resource, even if a legacy caller tries it. */
    if(index==6)return -EBUSY;
    if(test_backing){modes[index]=mode;return 0;}
    if(!owned[index]){
        struct gpio_desc *desc=gpiochip_request_own_desc(banks[index/8].chip,index%8,"steamlink-av",GPIO_LOOKUP_FLAGS_DEFAULT,mode==1?GPIOD_IN:GPIOD_OUT_LOW);
        if(IS_ERR(desc))return PTR_ERR(desc);
        owned[index]=desc;result=0;
    }else result=mode==1?gpiod_direction_input(owned[index]):gpiod_direction_output_raw(owned[index],values[index]);
    if(!result)modes[index]=mode;
    return result;
}
static int read_port(unsigned int index)
{return test_backing?values[index]:gpiod_get_raw_value_cansleep(descriptor(index));}
static long gpio_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
    bool sm=cmd>=0x3333&&cmd<=0x3338;unsigned int operation=sm?cmd-0x2222:cmd;
    struct legacy_gpio request;long result=0;int index;
    if(operation==0x1114){
        struct legacy_regs snapshot={0};
        mutex_lock(&gpio_lock);
        for(unsigned int p=0;p<(sm?16:32);p++){
            unsigned int i=p+(sm?32:0);int value=read_port(i);
            int direction=test_backing?(modes[i]!=2):gpiod_get_direction(descriptor(i));
            if(value<0||direction<0){result=-EIO;break;}
            if(value)snapshot.data|=BIT(p);if(direction)snapshot.direction|=BIT(p);
            snapshot.modes[p]=modes[i];
        }
        mutex_unlock(&gpio_lock);
        return result?result:copy_to_user((void __user *)arg,&snapshot,sizeof(snapshot))?-EFAULT:0;
    }
    if(operation!=0x1111&&operation!=0x1112&&operation!=0x1113)return -EOPNOTSUPP;
    if(copy_from_user(&request,(void __user *)arg,sizeof(request)))return -EFAULT;
    index=port_index(sm,request.port);if(index<0)return index;
    mutex_lock(&gpio_lock);
    if(operation==0x1113)result=set_mode(index,request.mode);
    else if(operation==0x1111){
        if(modes[index]!=1)result=-ENODEV;
        else {int value=read_port(index);if(value<0)result=value;else{request.data=value;result=copy_to_user((void __user *)arg,&request,sizeof(request))?-EFAULT:0;}}
    }else{
        if(modes[index]!=2)result=-ENODEV;
        else {values[index]=!!request.data;if(!test_backing)gpiod_set_raw_value_cansleep(owned[index],values[index]);}
    }
    mutex_unlock(&gpio_lock);return result;
}
static const struct file_operations gpio_ops={.owner=THIS_MODULE,.open=nonseekable_open,.unlocked_ioctl=gpio_ioctl};
static struct miscdevice gpio_device={.minor=116,.name="gpio",.fops=&gpio_ops,.mode=0600};
static void release_banks(void)
{
    for(unsigned int i=0;i<48;i++)if(owned[i])gpiochip_free_own_desc(owned[i]);
    for(unsigned int i=0;i<6;i++)if(banks[i].device)gpio_device_put(banks[i].device);
}
static int __init gpio_init(void)
{
    static const char *paths[]={"/soc@f7000000/apb@e80000/gpio@400","/soc@f7000000/apb@e80000/gpio@800",
        "/soc@f7000000/apb@e80000/gpio@c00","/soc@f7000000/apb@e80000/gpio@1000",
        "/soc@f7000000/apb@fc0000/gpio@c000","/soc@f7000000/apb@fc0000/gpio@5000"};
    int result;
    if(!enable)return -EPERM;
    if(test_backing){if(!of_machine_is_compatible("linux,dummy-virt"))return -EPERM;}
    else {
        if(!of_machine_is_compatible("valve,steamlink"))return -EPERM;
        for(unsigned int i=0;i<6;i++){
            struct find_bank find={.node=of_find_node_by_path(paths[i])};
            if(!find.node){result=-ENODEV;goto fail;}
            banks[i].device=gpio_device_find(&find,match_bank);banks[i].chip=find.chip;of_node_put(find.node);
            if(!banks[i].device){result=-ENODEV;goto fail;}
        }
    }
    result=misc_register(&gpio_device);if(!result)return 0;
fail:release_banks();return result;
}
static void __exit gpio_exit(void){misc_deregister(&gpio_device);release_banks();}
module_init(gpio_init);module_exit(gpio_exit);MODULE_LICENSE("GPL");
