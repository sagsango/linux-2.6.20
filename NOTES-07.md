# overview
Boot →
 └─ Kernel initializes IDE core
      ├─ Detect controllers (hwif)
      ├─ Probe drives (drive)
      ├─ Match media type (disk/cdrom/tape)
      ├─ Bind ide-disk driver
      └─ Register block device /dev/hdX


# from boot to ide subsystem
start_kernel()
 ├─ rest_init()
 ├─ driver_init()
 │    ├─ pci_init()                 ← if PCI subsystem present
 │    ├─ ide_init()                 ← in drivers/ide/ide.c
 │    └─ ...



ide_init()
 ├─ ide_setup_pci_devices()         ← detect PCI IDE controllers
 ├─ ide_init_builtin_drivers()      ← register chipset drivers
 ├─ ide_scan_pcibus()               ← enumerate controllers
 ├─ probe_hwif_init()               ← build ide_hwif_t for each
 └─ ide_register_driver(ide_generic)


# controller/channel creation
              +-----------------------------------------+
              | ide_hwif_t                              |
              +-----------------------------------------+
              | io_ports[8] → base I/O addresses        |
              | irq → 14                                |
              | index → 0  (ide0 channel)               |
              | drives[0] → hda                         |
              | drives[1] → hdb                         |
              | hwgroup (shared locks, DMA info)        |
              +-----------------------------------------+


ide_hwif_t *hwif = &ide_hwifs[index];
hwif->io_ports[IDE_DATA_OFFSET] = 0x1f0;
hwif->irq = 14;
ide_init_port_data(hwif);
ide_probe_port(hwif);

IDE default I/O/IRQ comes from asm-i386/ide.h:
base 0x1f0 → irq 14
base 0x170 → irq 15

# IDE default I/O/IRQ comes from asm-i386/ide.h:
each channel can hve up to 2 devices master and slave
probe_hwif_init()
 ├─ probe_for_drives(hwif)
 │    ├─ issue IDENTIFY command (0xEC)
 │    ├─ read 512-byte id block
 │    ├─ fill struct hd_driveid
 │    ├─ fill ide_drive_t
 │    └─ mark drive->present = 1


          +-----------------------------------+
          | ide_drive_t (represents /dev/hdX) |
          +-----------------------------------+
          | name = "hda"                      |
          | hwif = &ide_hwifs[0]              |
          | select.b.unit = 0 (master)        |
          | id = struct hd_driveid *id_data   |
          | media = ide_disk                  |
          | driver = NULL (not bound yet)     |
          +-----------------------------------+


# matching driver
ide_bus_type  (drivers/ide/ide.c)
     |
     ├─ ide-disk.c
     ├─ ide-cd.c
     ├─ ide-tape.c
     └─ ...


idedisk_init()
 └─ driver_register(&idedisk_driver.gen_driver)
       │
       └──→ adds to ide_bus_type


for each ide_drive_t where media == ide_disk
     → call ide_disk_probe()



# ide_disk_probe(drive)
ide_disk_probe(drive)
 ├─ Allocate ide_disk_obj
 │     ├─ links ide_drive_t + ide_driver_t + gendisk
 │     └─ holds kref for lifetime mgmt
 ├─ Allocate gendisk (alloc_disk_node())
 ├─ ide_init_disk(g, drive)
 ├─ ide_register_subdriver(drive, &idedisk_driver)
 ├─ idedisk_setup(drive)
 │     ├─ identify LBA/LBA48
 │     ├─ read capacity
 │     ├─ enable DMA/write cache
 │     └─ print drive banner
 ├─ set_capacity(g, capacity)
 ├─ g->fops = &idedisk_ops
 ├─ add_disk(g)
 └─ /dev/hdX created


# Data structure relationship
                +-------------------------------------+
                | ide_driver_t idedisk_driver         |
                +-------------------------------------+
                | .probe = ide_disk_probe             |
                | .do_request = ide_do_rw_disk        |
                | .media = ide_disk                   |
                +-------------------------------------+
                               │
                               ▼
                +-------------------------------------+
                | ide_drive_t hda                     |
                +-------------------------------------+
                | .media = ide_disk                   |
                | .driver_data = ide_disk_obj         |
                | .queue → blk request queue          |
                | .capacity64 = # of sectors           |
                | .bios_cyl/head/sect (geometry)      |
                +-------------------------------------+
                               │
                               ▼
                +-------------------------------------+
                | ide_disk_obj                        |
                +-------------------------------------+
                | .drive = ide_drive_t *              |
                | .driver = ide_driver_t *            |
                | .disk = struct gendisk *            |
                +-------------------------------------+
                               │
                               ▼
                +-------------------------------------+
                | struct gendisk                      |
                +-------------------------------------+
                | .major = IDE0_MAJOR (3)             |
                | .first_minor = 0                    |
                | .fops = idedisk_ops                 |
                | .private_data = &idkp->driver       |
                +-------------------------------------+


# flow
                +-----------------------------------------------+
                | start_kernel()                                |
                +-----------------------------------------------+
                                │
                                ▼
                +-----------------------------------------------+
                | ide_init()                                    |
                |  → ide_scan_pcibus()                          |
                |  → probe_hwif_init()                          |
                +-----------------------------------------------+
                                │
                                ▼
                +-----------------------------------------------+
                | create ide_hwif_t (ide0)                      |
                |  io = 0x1f0 irq=14                            |
                +-----------------------------------------------+
                                │
                                ▼
                +-----------------------------------------------+
                | probe_for_drives(hwif)                        |
                |  issue IDENTIFY to master/slave               |
                |  fill ide_drive_t                             |
                |   → hda.present=1 media=ide_disk              |
                +-----------------------------------------------+
                                │
                                ▼
                +-----------------------------------------------+
                | idedisk_init()                                |
                |  driver_register(&idedisk_driver)             |
                +-----------------------------------------------+
                                │
                                ▼
                +-----------------------------------------------+
                | ide core matches media=ide_disk               |
                |  → ide_disk_probe(hda)                        |
                +-----------------------------------------------+
                                │
                                ▼
                +-----------------------------------------------+
                | ide_disk_probe()                              |
                |  alloc ide_disk_obj                           |
                |  alloc gendisk                                |
                |  ide_init_disk()                              |
                |  idedisk_setup()                              |
                |  add_disk()                                   |
                +-----------------------------------------------+
                                │
                                ▼
          +------------------------------------------+
          | /dev/hda block device ready              |
          | /proc/ide/hda/* entries created          |
          +------------------------------------------+


# ide_disk_setup() inner flow:
idedisk_setup(drive)
 ├─ idedisk_add_settings(drive)
 ├─ set_lba_addressing(drive,1)
 ├─ init_idedisk_capacity(drive)
 │     ├─ if id->command_set_2 has LBA48 → lba_capacity_2
 │     ├─ else use lba_capacity (28-bit)
 │     └─ else fallback to CHS
 ├─ check for Host Protected Area (HPA)
 ├─ compute geometry (bios_cyl,head,sect)
 ├─ print summary banner
 ├─ write_cache(drive,1)
 └─ update_ordered()  → barrier/flush setup

hda: 390721968 sectors (200 GB) w/8192KiB Cache, CHS=16383/255/63, UDMA/100


# after boot
User read() → VFS
    → generic_file_read()
        → submit_bio()
            → generic_make_request()
                → drive->queue (blk_queue)
                    → ide_do_request()
                        → ide_do_rw_disk()
                            → __ide_do_rw_disk()
                                 ├─ program taskfile regs
                                 ├─ choose DMA/PIO
                                 ├─ send WIN_READ/_WRITE cmd
                                 └─ IRQ → ide_intr() → ide_end_request()


# where driver selection happen
When kernel boots:
  For each ide_drive_t (hda, hdb, etc):
     if drive->media == ide_disk → ide-disk
     if drive->media == ide_cdrom → ide-cd
     if drive->media == ide_tape → ide-tape


media filed is set in the IDENTIFY data:
if (id->config & 0x0080)
     drive->media = ide_cdrom;
else
     drive->media = ide_disk;

# complete
BIOS/Firmware
  ↓
PCI/ISA Detection
  ↓
ide_init() → probe_hwif_init()
  ↓
create ide_hwif_t (controller)
  ↓
issue IDENTIFY → create ide_drive_t (drive)
  ↓
set drive->media = ide_disk
  ↓
idedisk_init() registers ide-disk driver
  ↓
ide core matches & calls ide_disk_probe()
  ↓
idedisk_setup() reads capacity, enables DMA/cache
  ↓
add_disk() registers block dev
  ↓
/dev/hdX becomes ready

