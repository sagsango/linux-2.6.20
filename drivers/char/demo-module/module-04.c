/*
 * demo_module.c
 *
 * Version 4:
 *   - character device
 *   - read()
 *   - write()
 *   - ioctl()
 *
 * Linux 2.6.20 style uses .ioctl, not .unlocked_ioctl.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "demo_ioctl"
#define BUF_SIZE 128

#define DEMO_IOCTL_CLEAR      0
#define DEMO_IOCTL_GET_SIZE   1
#define DEMO_IOCTL_SET_VALUE  2
#define DEMO_IOCTL_GET_VALUE  3

static dev_t demo_dev;
static struct cdev demo_cdev;

static char kernel_buf[BUF_SIZE];
static int data_size;
static int demo_value = 100;

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

	printk(KERN_INFO "demo_ioctl: read %zu bytes\n", count);

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

	printk(KERN_INFO "demo_ioctl: wrote %zu bytes: %s\n",
	       count, kernel_buf);

	return count;
}

static int demo_ioctl(struct inode *inode,
		      struct file *file,
		      unsigned int cmd,
		      unsigned long arg)
{
	int value;

	switch (cmd) {
	case DEMO_IOCTL_CLEAR:
		memset(kernel_buf, 0, BUF_SIZE);
		data_size = 0;
		printk(KERN_INFO "demo_ioctl: buffer cleared\n");
		return 0;

	case DEMO_IOCTL_GET_SIZE:
		if (put_user(data_size, (int __user *)arg))
			return -EFAULT;
		printk(KERN_INFO "demo_ioctl: get size = %d\n", data_size);
		return 0;

	case DEMO_IOCTL_SET_VALUE:
		if (get_user(value, (int __user *)arg))
			return -EFAULT;
		demo_value = value;
		printk(KERN_INFO "demo_ioctl: set value = %d\n", demo_value);
		return 0;

	case DEMO_IOCTL_GET_VALUE:
		if (put_user(demo_value, (int __user *)arg))
			return -EFAULT;
		printk(KERN_INFO "demo_ioctl: get value = %d\n", demo_value);
		return 0;

	default:
		printk(KERN_INFO "demo_ioctl: unknown ioctl cmd=%u\n", cmd);
		return -ENOTTY;
	}
}

static struct file_operations demo_fops = {
	.owner = THIS_MODULE,
	.read  = demo_read,
	.write = demo_write,
	.ioctl = demo_ioctl,
};

static int __init demo_init(void)
{
	int ret;

	printk(KERN_INFO "demo_ioctl: init\n");

	ret = alloc_chrdev_region(&demo_dev, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		printk(KERN_ERR "demo_ioctl: alloc_chrdev_region failed\n");
		return ret;
	}

	cdev_init(&demo_cdev, &demo_fops);
	demo_cdev.owner = THIS_MODULE;

	ret = cdev_add(&demo_cdev, demo_dev, 1);
	if (ret < 0) {
		printk(KERN_ERR "demo_ioctl: cdev_add failed\n");
		unregister_chrdev_region(demo_dev, 1);
		return ret;
	}

	printk(KERN_INFO "demo_ioctl: registered major=%d minor=%d\n",
	       MAJOR(demo_dev), MINOR(demo_dev));
	printk(KERN_INFO "demo_ioctl: create node:\n");
	printk(KERN_INFO "mknod /dev/demo_ioctl c %d %d\n",
	       MAJOR(demo_dev), MINOR(demo_dev));

	return 0;
}

static void __exit demo_exit(void)
{
	cdev_del(&demo_cdev);
	unregister_chrdev_region(demo_dev, 1);

	printk(KERN_INFO "demo_ioctl: exit\n");
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sagar Singh");
MODULE_DESCRIPTION("Linux 2.6.20 char device with read/write/ioctl");
