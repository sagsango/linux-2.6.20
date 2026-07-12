##### This is after the boot; device discovery already happed #####
Boot Scan Flow (Hardware Arrives First)
	[Physical Scan] ──► device_add() ──► bus_for_each_drv() ──┐
        	                                                  ├─► driver_probe_device() ──► __pci_device_probe()
Module Load Flow (Software Arrives Second) 	                  │
   [insmod command] ──► driver_register() ──► bus_for_each_dev()  ┘




========================================================================================================
PATHWAY A: BOOT DISCOVERY (Hardware Arrives First)     │ PATHWAY B: MODULE LOADING (Software Arrives Second)
========================================================================================================
                                                       │
[ Motherboard Power On & Core Initialization ]         │ [ User Executes Command Line Injection ]
       │                                               │        │
       ▼ (arch/i386/pci/init.c)                        │        ▼ (kernel/module.c)
 pcibios_init()                                        │  sys_init_module()
       │                                               │        │  (Copies .ko binary into kernel space)
       ▼ (drivers/pci/probe.c)                         │        ▼
 pci_scan_child_bus()                                  │  mod->init() 
       │  (Nested BDF loops: Buses 0-255)              │        │  (Executes your module_init() function)
       ▼                                               │        ▼ (your_driver.c)
 pci_scan_slot()                                       │  pci_register_driver(&your_pci_driver)
       │  (Loops through slots 0-31)                   │        │
       ▼                                               │        ▼ (drivers/pci/pci-driver.c)
 pci_bus_read_config_dword()                           │  driver_register(&drv->driver)
       │  (Reads raw hardware Vendor/Device IDs)       │        │  (Appends driver to pci_bus_type list)
       ▼                                               │        ▼ (drivers/base/bus.c)
 pci_device_add()                                      │  bus_add_driver(drv)
       │  (Allocates and populates struct pci_dev)     │        │
       ▼ (drivers/base/core.c)                         │        ▼ (drivers/base/dd.c)
 device_add(struct device *dev)                        │  driver_attach(struct device_driver *drv)
       │  (Registers device profile inside sysfs)      │        │
       ▼ (drivers/base/dd.c)                           │        ▼ (drivers/base/bus.c)
 device_attach(struct device *dev)                     │  bus_for_each_dev(drv->bus, ..., __driver_attach)
       │                                               │        │  (Loops through every ALREADY DISCOVERED
       ▼ (drivers/base/bus.c)                          │        │   unassigned pci_dev profile on the bus)
 bus_for_each_drv(dev->bus, ..., __device_attach)      │        │
       │  (Loops through every ALREADY LOADED          │        │
       │   pci_driver structure in memory)             │        │
       ▼ (drivers/base/dd.c)                           │        ▼ (drivers/base/dd.c)
 __device_attach(driver, device)                       │  __driver_attach(device, driver)
       │                                               │        │
       └───────────────────────┬───────────────────────┘        │
                               │                                │
                               ▼ (drivers/base/bus.c)           │
                        drv->bus->match()                       │
                        [Points to: pci_bus_match()]            │
                               │                                │
                     Does hardware Vendor/Device                │
                     ID match the driver table?                 │
                               │                                │
                      ┌────────┴────────┐                       │
                      ▼ (Yes)           ▼ (No)                  │
                      │            [Skip / Next]                │
                      ▼                                         ▼
========================================================================================================
THE CONVERGENCE POINT (The Wedding)
========================================================================================================
                               │
                               ▼ (drivers/base/dd.c)
                     driver_probe_device(drv, dev)
                               │  (Acquires infrastructure safety locks)
                               ▼
                     dev->bus->probe(dev) 
                     [Points to: pci_device_probe()]
                               │
                               ▼ (drivers/pci/pci-driver.c)
                     __pci_device_probe(pci_drv, pci_dev)
                               │  (Casts generic device to struct pci_dev)
                               │  (Binds tracking pointer: pci_dev->driver = drv)
                               ▼
                     drv->probe(pci_dev, pci_device_id)
                               │  (Jumps into your custom storage driver code!)
                               │
                               ▼ (your_driver.c)
                     [ Your Driver Setup Logic Runs ]
                               │  (Initializes card registers and memory ranges)
                               ▼ (block/genhd.c)
                     add_disk(struct gendisk *disk)
                               │  (Registers major/minor regions inside bdev_map)
                               ▼ (drivers/base/core.c)
                     device_add() -> kobject_uevent()
                               │  (Broadcasts Netlink environment blocks)
                               ▼
                     ===================================================
                     USER SPACE: udevd intercepts -> calls mknod() -> /dev/sda
                     =================================================
