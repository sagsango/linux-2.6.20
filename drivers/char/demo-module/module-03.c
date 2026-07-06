/*
 * demo_module.c
 *
 * Version 3:
 *   - module_init/module_exit
 *   - character device registration
 *   - read()
 *   - write()
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "demo_char"
#define BUF_SIZE 128

static dev_t demo_dev;
static struct cdev demo_cdev;

static char kernel_buf[BUF_SIZE];
static int data_size;

static ssize_t demo_read(struct file *file,
			 char __user *user_buf,
			 size_t count,
			 loff_t *ppos)
{
	int ret;

	if (*ppos >= data_size)
		return 0;

	if (count > data_size - *ppos)
		count = data_size - *ppos;

	ret = copy_to_user(user_buf, kernel_buf + *ppos, count);
	if (ret)
		return -EFAULT;

	*ppos += count;

	printk(KERN_INFO "demo_char: read %zu bytes\n", count);

	return count;
}

static ssize_t demo_write(struct file *file,
			  const char __user *user_buf,
			  size_t count,
			  loff_t *ppos)
{
	int ret;

	if (count > BUF_SIZE - 1)
		count = BUF_SIZE - 1;

	ret = copy_from_user(kernel_buf, user_buf, count);
	if (ret)
		return -EFAULT;

	kernel_buf[count] = '\0';
	data_size = count;
	*ppos = 0;

	printk(KERN_INFO "demo_char: wrote %zu bytes: %s\n",
	       count, kernel_buf);

	return count;
}

static struct file_operations demo_fops = {
	.owner = THIS_MODULE,
	.read  = demo_read,
	.write = demo_write,
};

static int __init demo_init(void)
{
	int ret;

	printk(KERN_INFO "demo_char: init\n");

	ret = alloc_chrdev_region(&demo_dev, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		printk(KERN_ERR "demo_char: alloc_chrdev_region failed\n");
		return ret;
	}

	cdev_init(&demo_cdev, &demo_fops);
	demo_cdev.owner = THIS_MODULE;

	ret = cdev_add(&demo_cdev, demo_dev, 1);
	if (ret < 0) {
		printk(KERN_ERR "demo_char: cdev_add failed\n");
		unregister_chrdev_region(demo_dev, 1);
		return ret;
	}

	printk(KERN_INFO "demo_char: registered major=%d minor=%d\n",
	       MAJOR(demo_dev), MINOR(demo_dev));

	printk(KERN_INFO "demo_char: create node with:\n");
	printk(KERN_INFO "mknod /dev/demo_char c %d %d\n",
	       MAJOR(demo_dev), MINOR(demo_dev));

	return 0;
}

static void __exit demo_exit(void)
{
	cdev_del(&demo_cdev);
	unregister_chrdev_region(demo_dev, 1);

	printk(KERN_INFO "demo_char: exit\n");
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sagar Singh");
MODULE_DESCRIPTION("Simple Linux 2.6.20 char device with read/write");
