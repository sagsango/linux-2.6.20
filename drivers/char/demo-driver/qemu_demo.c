#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>

#define DRV_NAME "qemu_demo"
#define VENDOR_ID 0x1234
#define DEVICE_ID 0x11e8

struct qemu_demo {
	struct pci_dev *pdev;
	void __iomem *bar0;
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	struct class_device *class_dev;
};

static int qemu_demo_open(struct inode *inode, struct file *file)
{
	struct qemu_demo *d;

	d = container_of(inode->i_cdev, struct qemu_demo, cdev);
	file->private_data = d;

	return 0;
}

static ssize_t qemu_demo_read(struct file *file,
			      char __user *buf,
			      size_t count,
			      loff_t *ppos)
{
	struct qemu_demo *d = file->private_data;
	u32 val;

	if (count < sizeof(val))
		return -EINVAL;

	val = ioread32(d->bar0 + 0x04);

	if (copy_to_user(buf, &val, sizeof(val)))
		return -EFAULT;

	return sizeof(val);
}

static ssize_t qemu_demo_write(struct file *file,
			       const char __user *buf,
			       size_t count,
			       loff_t *ppos)
{
	struct qemu_demo *d = file->private_data;
	u32 val;

	if (count < sizeof(val))
		return -EINVAL;

	if (copy_from_user(&val, buf, sizeof(val)))
		return -EFAULT;

	iowrite32(val, d->bar0 + 0x04);

	return sizeof(val);
}

static struct file_operations qemu_demo_fops = {
	.owner = THIS_MODULE,
	.open  = qemu_demo_open,
	.read  = qemu_demo_read,
	.write = qemu_demo_write,
};

static int qemu_demo_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	struct qemu_demo *d;
	int ret;

	printk(KERN_INFO DRV_NAME ": probe\n");

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret)
		goto err_disable;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		ret = -ENOMEM;
		goto err_regions;
	}

	pci_set_drvdata(pdev, d);
	d->pdev = pdev;

	d->bar0 = pci_iomap(pdev, 0, 0);
	if (!d->bar0) {
		ret = -ENOMEM;
		goto err_free;
	}

	ret = alloc_chrdev_region(&d->devt, 0, 1, DRV_NAME);
	if (ret)
		goto err_iomap;

	cdev_init(&d->cdev, &qemu_demo_fops);
	d->cdev.owner = THIS_MODULE;

	ret = cdev_add(&d->cdev, d->devt, 1);
	if (ret)
		goto err_chrdev;

	d->class = class_create(THIS_MODULE, DRV_NAME);
	if (IS_ERR(d->class)) {
		ret = PTR_ERR(d->class);
		goto err_cdev;
	}

	d->class_dev = class_device_create(d->class, NULL,
					   d->devt, NULL,
					   "qemu_demo0");
	if (IS_ERR(d->class_dev)) {
		ret = PTR_ERR(d->class_dev);
		goto err_class;
	}

	printk(KERN_INFO DRV_NAME ": registered /dev/qemu_demo0\n");
	return 0;

err_class:
	class_destroy(d->class);
err_cdev:
	cdev_del(&d->cdev);
err_chrdev:
	unregister_chrdev_region(d->devt, 1);
err_iomap:
	pci_iounmap(pdev, d->bar0);
err_free:
	kfree(d);
err_regions:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	return ret;
}

static void qemu_demo_remove(struct pci_dev *pdev)
{
	struct qemu_demo *d = pci_get_drvdata(pdev);

	class_device_destroy(d->class, d->devt);
	class_destroy(d->class);
	cdev_del(&d->cdev);
	unregister_chrdev_region(d->devt, 1);

	pci_iounmap(pdev, d->bar0);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	kfree(d);

	printk(KERN_INFO DRV_NAME ": removed\n");
}

static struct pci_device_id qemu_demo_ids[] = {
	{ PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, qemu_demo_ids);

static struct pci_driver qemu_demo_driver = {
	.name     = DRV_NAME,
	.id_table = qemu_demo_ids,
	.probe    = qemu_demo_probe,
	.remove   = qemu_demo_remove,
};

static int __init qemu_demo_init(void)
{
	return pci_register_driver(&qemu_demo_driver);
}

static void __exit qemu_demo_exit(void)
{
	pci_unregister_driver(&qemu_demo_driver);
}

module_init(qemu_demo_init);
module_exit(qemu_demo_exit);

MODULE_LICENSE("GPL");
