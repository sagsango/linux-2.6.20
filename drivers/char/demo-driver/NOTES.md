# Flow
QEMU device
   |
   v
PCI bus exposes device
   |
   v
Linux PCI core detects it
   |
   v
Your pci_driver matches vendor/device ID
   |
   v
probe() runs
   |
   v
driver maps BAR / registers char device
   |
   v
/dev/mydev0 appears



# Driver lifecycle
module_init()
   |
   v
pci_register_driver()
   |
   v
PCI core matches device ID
   |
   v
probe()
   |
   +--> pci_enable_device()
   +--> pci_request_regions()
   +--> pci_iomap()
   +--> alloc_chrdev_region()
   +--> cdev_add()
   +--> class_create()
   +--> class_device_create()
   |
   v
/dev/qemu_demo0




# remove
rmmod
 |
 v
remove()
 |
 +--> class_device_destroy()
 +--> class_destroy()
 +--> cdev_del()
 +--> unregister_chrdev_region()
 +--> pci_iounmap()
 +--> pci_release_regions()
 +--> pci_disable_device()



# Big picture
QEMU fake hardware
   |
PCI enumeration
   |
Linux pci_dev
   |
your pci_driver
   |
probe()
   |
MMIO BAR mapped
   |
char device created
   |
/dev/qemu_demo0
   |
userspace read/write



# Debugging
1. Check all the visible hardware devices
lspci -nn
2. Check driver's is hooked properly
{ PCI_DEVICE(0x1234, 0x11e8) }
3. 
