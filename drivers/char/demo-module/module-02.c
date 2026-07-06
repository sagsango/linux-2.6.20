/*
 * demo_module.c
 *
 * Linux 2.6.20 learning module - Version 2
 * Demonstrates:
 *   - module_init()
 *   - module_exit()
 *   - printk()
 *   - module_param()
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

static int value = 10;
static char *name = "sagar";
static int debug = 0;

module_param(value, int, 0644);
MODULE_PARM_DESC(value, "Integer value for demo module");

module_param(name, charp, 0644);
MODULE_PARM_DESC(name, "Name string for demo module");

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable debug messages");

static int __init demo_init(void)
{
	printk(KERN_INFO "demo_module: loaded\n");
	printk(KERN_INFO "demo_module: value = %d\n", value);
	printk(KERN_INFO "demo_module: name = %s\n", name);
	printk(KERN_INFO "demo_module: debug = %d\n", debug);

	if (debug)
		printk(KERN_INFO "demo_module: debug mode enabled\n");

	return 0;
}

static void __exit demo_exit(void)
{
	printk(KERN_INFO "demo_module: unloaded\n");
	printk(KERN_INFO "demo_module: final value = %d\n", value);
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sagar Singh");
MODULE_DESCRIPTION("Linux 2.6.20 demo module version 2 with parameters");
