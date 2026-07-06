#include <linux/module.h>
#include <linux/init.h>

static int __init demo_init(void)
{
    printk(KERN_INFO "demo_module: loaded\n");
    return 0;
}

static void __exit demo_exit(void)
{
    printk(KERN_INFO "demo_module: unloaded\n");
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sagar");
MODULE_DESCRIPTION("Learning module for Linux 2.6.20");
