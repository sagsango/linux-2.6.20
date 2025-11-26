# Overview (hard disk initialization)
BIOS / Firmware
  ↓
bootloader loads vmlinuz + initrd
  ↓
start_kernel()
  ↓
  +------------------------------------------+
  |  kernel init phase                       |
  |------------------------------------------|
  |  driver_init()     → set up bus model    |
  |  ide_init()        → IDE subsystem init  |
  |  probe_hwif_init() → detect controllers  |
  |  probe_for_drives()→ detect drives       |
  |  idedisk_init()    → register driver     |
  |  driver_register() → bind driver↔device  |
  |  ide_disk_probe()  → create /dev/hdX     |
  +------------------------------------------+



# Core Initialization Context
start_kernel()
 ├─ driver_init()   ← initializes bus/device/class layers
 └─ ide_init()      ← registers "ide" bus and starts probing

driver_init() sets up the driver model:
/sys/bus/
    ├── pci/
    ├── ide/    ← will appear after ide_init()
    └── platform/



# IDE Bus Registration
ide_init()
 ├─ bus_register(&ide_bus_type);       ← registers "ide" bus
 │      name = "ide"
 │      .probe = generic_ide_probe
 │      .remove = generic_ide_remove
 │      .match = ide_bus_match (usually always true)
 │
 ├─ probe_hwif_init();                 ← find I/O ports for controllers
 ├─ hwif_init() per interface          ← fills ide_hwif_t
 │
 ├─ probe_for_drives(hwif)
 │     → identify drives via ATA commands
 │     → fill ide_drive_t
 │     → device_register(&drive->gendev)
 │
 └─ idedisk_init()                     ← register IDE disk driver



# Hardware Interface
For each IDE controller or channel, the kernel creates:
+--------------------------------------+
| struct ide_hwif_t                    |
+--------------------------------------+
| name        = "ide0"                 |
| index       = 0                      |
| io_ports[]  = {0x1f0, 0x3f6, ...}    |
| irq         = 14                     |
| drives[0]   = master (hda)           |
| drives[1]   = slave  (hdb)           |
| dma_base, chip, bus, etc.            |
+--------------------------------------+


ide_setup_ports()
  ↓
ide_init_port_data()
  ↓
probe_hwif_init()



# Driver Detection
Each hwif probes for two drives:
probe_for_drives(hwif)
 ├─ select master/slave
 ├─ send ATA IDENTIFY (0xEC)
 ├─ read 512-byte id block → struct hd_driveid
 ├─ fill struct ide_drive_t
 └─ device_register(&drive->gendev)


+-----------------------------------------------------+
| struct ide_drive_t                                  |
+-----------------------------------------------------+
| name         = "hda"                                |
| present      = 1                                    |
| media        = ide_disk                             |
| hwif         = pointer to ide_hwif_t (ide0)         |
| id           = hd_driveid (IDENTIFY data)           |
| select       = drive/head register values           |
| gendev       = struct device (for /sys & driver)    |
| queue        = request_queue for block I/O          |
| driver_data  = NULL (until bound)                   |
+-----------------------------------------------------+

This is now a device on the “ide” bus:
/sys/bus/ide/devices/hda
 ├── media = "disk"
 ├── drivename = "hda"
 ├── modalias = "ide:m-disk"
 └── uevent → informs udev

At this point the kernel knows about the disk but hasn’t assigned a driver yet.

# IDE-Disk Driver Registration (drivers/ide/ide-disk.c)
static int __init idedisk_init(void)
{
    return driver_register(&idedisk_driver.gen_driver);
}

struct ide_driver_t idedisk_driver = {
    .gen_driver.name = "ide-disk",
    .gen_driver.bus  = &ide_bus_type,
    .probe    = ide_disk_probe,
    .remove   = ide_disk_remove,
    .shutdown = ide_device_shutdown,
    .media    = ide_disk,
};


# Binding: driver_register() → bus_add_driver()
driver_register()
 └─ bus_add_driver()
      ├─ adds driver to ide_bus_type->p->klist_drivers
      ├─ creates /sys/bus/ide/drivers/ide-disk
      └─ for each device on bus:
           generic_ide_probe(dev)
              └─ drv->probe(drive)
                  = ide_disk_probe()



# driver registration:
driver_register()
 └─ bus_add_driver()
      ├─ adds driver to ide_bus_type->p->klist_drivers
      ├─ creates /sys/bus/ide/drivers/ide-disk
      └─ for each device on bus:
           generic_ide_probe(dev)
              └─ drv->probe(drive)
                  = ide_disk_probe()



# How driver is chosen
ide_disk_probs():
  if (!drive->present) return -ENODEV;
  if (drive->media != ide_disk) return -ENODEV;
  drive->media == ide_cdrom then ide-cd

# on media found
ide_disk_probe(drive)
 ├─ alloc ide_disk_obj
 │    ├─ contains (drive, driver, gendisk)
 ├─ alloc gendisk structure
 │    ├─ fops = idedisk_ops
 │    ├─ private_data = idkp
 │    ├─ driverfs_dev = &drive->gendev
 │    └─ minors = 16 (1 << PARTN_BITS)
 ├─ ide_init_disk(g, drive)
 │    ├─ sets queue, capacity, etc.
 │    └─ blk_queue setup
 ├─ idedisk_setup(drive)
 │    ├─ detect LBA / LBA48
 │    ├─ read capacity from IDENTIFY
 │    ├─ enable DMA, write cache
 │    ├─ print "hda: 100GB UDMA/100"
 ├─ set_capacity(g, drive->capacity)
 ├─ add_disk(g)
 │    ├─ register gendisk in block layer
 │    ├─ create /sys/block/hda/
 │    └─ make /dev/hda appear
 └─ return 0 (bound successfully)


# structures after sucessful bind
+--------------------------+      +-----------------------------+
| ide_driver_t             |      | ide_drive_t (hda)          |
|  .gen_driver = ide-disk  |◀───→ | .gendev.driver = ide-disk  |
|  .klist_devices = [hda]  |      | .driver_data = ide_disk_obj|
+--------------------------+      | .queue, .capacity, ...     |
                                  +-----------------------------+
                                              │
                                              ▼
                                  +-----------------------------+
                                  | ide_disk_obj                |
                                  |  .drive  = ide_drive_t*     |
                                  |  .disk   = gendisk*         |
                                  |  .driver = ide_driver_t*    |
                                  +-----------------------------+
                                              │
                                              ▼
                                  +-----------------------------+
                                  | struct gendisk (block dev)  |
                                  |  name = "hda"               |
                                  |  fops = idedisk_ops         |
                                  |  capacity = n sectors       |
                                  |  request_queue = blk queue  |
                                  +-----------------------------+


# device model & sysfs links
/sys/bus/ide/
├── devices/
│   └── hda/
│       ├── media="disk"
│       ├── driver → ../../drivers/ide-disk
│       └── uevent (MODALIAS=ide:m-disk)
└── drivers/
    └── ide-disk/
        └── hda → ../../devices/hda


# block device visible to user
add_disk() will regiter the disk in the block layer
/sys/block/hda/
   ├── size
   ├── queue/
   ├── stat
   └── device → ../../bus/ide/devices/hda

/dev/hda

udev or static device nodes now expose /dev/hda.

# io path once running
read()/write() syscall
     ↓
VFS → block layer → request_queue
     ↓
ide_do_rw_disk()
     ↓
__ide_do_rw_disk()
     ↓
   if DMA: setup PRD table, start DMA
   if PIO: write taskfile regs (LBA, cmd)
     ↓
IDE interrupt handler
     ↓
ide_end_request()
     ↓
I/O completes


# full boot-to-disk
start_kernel()
  │
  ├─ driver_init()
  │     ├─ devices_init()
  │     ├─ buses_init()
  │     └─ classes_init()
  │
  ├─ ide_init()
  │     ├─ bus_register(&ide_bus_type)
  │     ├─ probe_hwif_init()
  │     │     ├─ detect controllers (io=0x1f0, irq=14)
  │     │     └─ create ide_hwif_t
  │     ├─ probe_for_drives()
  │     │     ├─ send IDENTIFY
  │     │     ├─ fill ide_drive_t
  │     │     └─ device_register(&drive->gendev)
  │     └─ idedisk_init()
  │           └─ driver_register(&idedisk_driver)
  │                 └─ bus_add_driver()
  │                       └─ generic_ide_probe()
  │                             └─ ide_disk_probe()
  │                                   ├─ alloc ide_disk_obj
  │                                   ├─ alloc gendisk
  │                                   ├─ setup capacity, DMA
  │                                   ├─ add_disk()
  │                                   └─ /dev/hda ready
  │
  └─ mount_root → /dev/hda1



# call graph:
idedisk_init()                                   (drivers/ide/ide-disk.c)
  └─ driver_register(&idedisk_driver.gen_driver)  (your driver.c)
       └─ bus_add_driver()
            ├─ link driver into bus "ide"
            ├─ sysfs: /sys/bus/ide/drivers/ide-disk
            └─ for dev in bus->devices:
                 ├─ dev->bus == ide_bus_type ?
                 │    yes →
                 │      dev->driver = &idedisk_driver.gen_driver
                 │      bus->probe(dev)  == generic_ide_probe(dev)
                 │         └─ drv = to_ide_driver(dev->driver)
                 │         └─ return drv->probe(drive)   (ide_disk_probe)
                 │              ├─ alloc ide_disk_obj + gendisk
                 │              ├─ idedisk_setup() (LBA/LBA48, DMA, HPA)
                 │              ├─ set_capacity(), fops = idedisk_ops
                 │              └─ add_disk()  → /dev/hdX
                 └─ next device



