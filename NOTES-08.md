# flow
start_kernel()
  → rest_init()
     → init()
        → do_basic_setup()
           → driver_init()
           → do_initcalls()
              → subsys_initcall(ide_init)
                   ├─ ide_pci_init() → PCI IDE detection
                   └─ ide_probe_hwifs()
                         ├─ for i in [0..MAX_HWIFS)
                         │     base = ide_default_io_base(i)
                         │     irq  = ide_default_irq(base)
                         │     ide_init_hwif_ports(&hw, base, base+0x206, &irq)
                         │     ide_register_hw(&hw, &hwif)
                         └─ probe_hwif_init(hwif)
                              ├─ IDENTIFY DEVICE (0xEC)
                              └─ register drives (ide_drive_t)


# flow
start_kernel()
   → rest_init()
       → kernel_thread(init)
           → do_basic_setup()
               → driver_init()
               → do_initcalls()
                    → subsys_initcall(ide_init)
                         → ide_pci_init() / ide_generic_init()
                               → pci_register_driver()
                                   → probe() builds hw_regs_t
                                   → ide_register_hw(&hw, &hwif)
                                   → probe_hwif_init(hwif)
                                        → detect drives
                                        → fill ide_drive_t[]
                    → device_initcall(ide_disk_init)
                         → ide_register_driver(&ide_disk_driver)
                         → probe() binds to drives
                         → add_disk()



# ide.h
   [User of block layer]
        │
        ▼
 ┌─────────────────────────────┐
 │      ide_driver_t           │  ← ide-disk, ide-cd, etc.
 └─────────────────────────────┘
        │ probe(), do_request()
        ▼
 ┌─────────────────────────────┐
 │      ide_drive_t            │  ← represents one disk drive (hda)
 └─────────────────────────────┘
        │ part of
        ▼
 ┌─────────────────────────────┐
 │      ide_hwif_t             │  ← host interface (primary channel)
 └─────────────────────────────┘
        │ grouped by
        ▼
 ┌─────────────────────────────┐
 │      ide_hwgroup_t          │  ← coordination (shared IRQ)
 └─────────────────────────────┘


# struct ide_hwif_s
ide_hwif_t
├─ name: "ide0"
├─ io_ports[]       → I/O base 0x1F0, control 0x3F6
├─ irq              → 14
├─ drives[2]        → master/slave (ide_drive_t)
├─ dma*             → DMA registers & scatter-gather table
├─ pci_dev          → PCI controller owning it
├─ ops (function ptrs)
│    ├─ tuneproc()          (set PIO mode)
│    ├─ speedproc()         (set DMA/UDMA)
│    ├─ selectproc()        (chip-specific select)
│    ├─ dma_setup/start/end/check()
│    ├─ INB/OUTB, INSW/OUTSW, etc.
└─ flags (present, serialized, autodma, etc.)

# struct ide_drive_s
char name[4]        -> "hda"
struct hwif_s *hwif  -> back-pointer to channel
struct hd_driveid *id  -> IDENTIFY DEVICE data
struct gendisk *disk   -> block layer
struct request_queue *queue
u8 using_dma, autodma
u64 capacity64
flags: present, removable, noprobe, etc.
struct device gendev   -> ties into /sys/block


# struct ide_driver_s
struct ide_driver_s {
    const char *version;
    u8 media;                 // ide_disk, ide_cdrom, etc.
    struct device_driver gen_driver;
    int  (*probe)(ide_drive_t *);
    void (*remove)(ide_drive_t *);
    ide_startstop_t (*do_request)(ide_drive_t *, struct request *, sector_t);
};


# struct hw_regs_s:
struct hw_regs_s {
    unsigned long io_ports[IDE_NR_PORTS];  // 0x1F0..0x1F7, 0x3F6, irq
    int irq;
    int dma;
    ide_ack_intr_t *ack_intr;
    hwif_chipset_t chipset;
    struct device *dev;
};


# struct ide_hwgroups:
ide_hwgroup_t
├─ handler() / expiry()
├─ drive (currently active)
├─ hwif (linked list of interfaces)
├─ timer (timeout)
├─ pci_dev (controller)
└─ flags: busy, sleeping, polling, resetting

# struct ide_pci_device_s
struct ide_pci_device_s {
    char *name;
    int  (*init_setup)(struct pci_dev *, struct ide_pci_device_s *);
    void (*init_hwif)(ide_hwif_t *);
    void (*init_dma)(ide_hwif_t *, unsigned long);
    u8   channels;   // 1 or 2
    u8   autodma;
    ide_pci_enablebit_t enablebits[2];
};


# init flow
do_basic_setup()
  └─ driver_init()         // sets up bus/class/core model
  └─ do_initcalls()
       ├─ ... core_initcall()s ...
       ├─ subsys_initcall(ide_init)        ← IDE core comes up
       │     ├─ register IDE class/port ops
       │     ├─ enumerate legacy I/O ports (0x1F0/0x170) if enabled
       │     └─ register IDE bus w/ driver model
       ├─ device_initcall(ide_pci_init)    ← PCI IDE host drivers register
       │     └─ pci_register_driver() → for each IDE controller:
       │           probe() → setup hwifs, BARs/IRQs, DMA engine
       │           probe channel(s) → IDENTIFY DEVICE (0xEC)
       │           create ide_drive_s for master/slave
       ├─ device_initcall(ide_disk_init)   ← ide-disk driver registers
       │     └─ ide_register_driver(ide_disk_driver)
       │           probe(drive) → alloc_disk(), request_queue, set_capacity()
       │           add_disk() → sysfs + uevent → udev creates /dev/hdX
       └─ ... late_initcall()s ...






# init flow
[bootloader] → start_kernel()
      │
      ├─ sched_init()         (scheduler online)
      ├─ init_IRQ()           (interrupts ready)
      ├─ ... memory/VFS/security/…
      └─ rest_init()
           ├─ kernel_thread(init)     → PID 1 (kernel thread)
           ├─ schedule()              → first context switch
           └─ cpu_idle()              → idle loop

init()  (PID 1)
  ├─ smp_*(), sched_init_smp()
  └─ do_basic_setup()
       ├─ driver_init()        (device model/buses/classes)
       └─ do_initcalls()
            ├─ subsys_initcall(ide_init)      (IDE core)
            ├─ device_initcall(ide_pci_init)  (PCI IDE host → probe hwifs)
            └─ device_initcall(ide_disk_init) (ide-disk binds & add_disk)
                   ↓
                sysfs + uevent → udev → /dev/hdX

  ├─ (initramfs?) run_init_process("/init")
  ├─ else prepare_namespace(); mount rootfs
  └─ run_init_process("/sbin/init") … fallbacks …



# flow
[BIOS/PCI config]
       │
       ▼
ide_init()
 ├─ ide_pci_init()
 │     └─ pci_register_driver(ide_pci_driver)
 │            └─ probe() → ide_setup_pci_controller()
 │                    └─ ide_init_hwif_ports()
 │                          └─ ide_identify_device()
 │                                └─ ide_register_device()
 │                                      └─ ide_disk_probe()
 │                                            └─ alloc_disk()
 │                                            └─ add_disk()
 │                                                 └─ udev → /dev/hda

