# Master
Kernel Boot Sequence (init/main.c)
    |
    +--> Early Arch Init (paging, IRQs, smp_init())
    |     [e.g., x86: setup_idt() --> enable i8259 PIC]
    |
    v
start_kernel()  [init/main.c: ~line 700]
    |
    +--> parse_early_param()  [boot params like ide=debug, hda=ide-disk]
    |     - If idebus=58 (e.g.), limit to 3 hwifs
    |     - If hdx=cdrom, force media=ide_cdrom during probe
    |
    +--> do_basic_setup() --> do_initcalls()  [initcall levels: pure->core->postcore->arch->subsys->...]
          |
          +--> PCI Init First (subsys_initcall(pci_init))  [drivers/pci/pci.c]
          |     |
          |     +--> pci_register_driver(&ide_pci_driver) for chips (e.g., piix.c)
          |     |     |
          |     |     +--> ide_pci_probe(pdev) --> alloc_hwif_ports(pci_dev=pdev, io=0x1F0)
          |     |           |
          |     |           +--> If PCI: Set hwif->pci_dev = pdev; hwif->dma_base = pci_resource_start()
          |     |
          |     +--> Legacy fallback if no PCI match
          |
          +--> IDE Core Init (subsys_initcall(ide_init))  [drivers/ide/ide.c: ~line 1500]
               |
               +--> INIT_WORK(&ide_work_queue.work, ide_work_queue_proc);  [async task queue for I/O]
               |
               +--> bus_register(&ide_bus_type);  [/sys/bus/ide/ created]
               |     [bus_type.name="ide", .match=ide_bus_match (by media/driver)]
               |
               +--> INIT_LIST_HEAD(&ide_hwifs); INIT_LIST_HEAD(&ide_drives);  [global lists]
               |
               +--> ide_probe_module();  [kick off hardware detection; ide-probe.c: ~line 200]
                    |
                    +--> If MAX_HWIFS==4 (default; param idebus= controls)
                         |
                         +--> Probe PCI Handlers (if any; from earlier pci_init)
                         |     |
                         |     +--> For each pci_dev in ide_pci_table (e.g., vendor=0x8086 PIIX):
                         |           |
                         |           +--> ide_pci_probe() --> alloc_hwif_from_pci(pci_dev)
                         |                 |
                         |                 +--> hwif->hwgroup = alloc_hwgroup();  [for multi-hwif sharing]
                         |                 +--> list_add(&hwif->list, &ide_hwifs);
                         |
                         +--> Probe Legacy Ports (sequential scan; no PCI override)
                              |
                              +--> For hwif_idx=0 to 3:  [primary: base=0x1F0, ctl=0x3F6, irq=14]
                                   |                       [secondary: base=0x170, ctl=0x376, irq=15]
                                   |                       [hwif_idx = (chip<<1) | unit; chip=0/1, unit=0/1]
                                   |
                                   +--> Check port free? (probe_ide_interface(base))
                                   |     |
                                   |     +--> N (e.g., other driver claims 0x1F0): Skip; printk("ide-probe: port busy")
                                   |
                                   +--> Y: alloc_hwif_ports(base, ctl, irq) --> kmalloc(sizeof(hwif_t), GFP_KERNEL)
                                         |
                                         +--> Init hwif_t:
                                         |     +--> hwif->name = "ide0"; hwif->index=0;
                                         |     +--> hwif->io_ports[IDE_DATA_OFFSET]=base+0;  [... up to IDE_NR_PORTS=10]
                                         |     +--> hwif->irq = irq; hwif->chipset = ide_generic;
                                         |     +--> hwif->selectproc = &default_select_proc;  [bit 4=slave]
                                         |     +--> spin_lock_init(&hwif->lock);  [protects I/O ops]
                                         |     +--> If DMA: hwif->dma_base = get_dma_base();  [param ide=dma checks]
                                         |
                                         +--> list_add_tail(&hwif->list, &ide_hwifs);  [global chain]
                                         |
                                         +--> request_region(base, 8, "ide");  [claim I/O ports]
                                         |     |
                                         |     +--> Fail? (conflict): kfree(hwif); continue;
                                         |
                                         +--> probe_hwif(hwif);  [per-interface drive probe; ide-probe.c: ~line 500]
                                              |
                                              +--> hwif->hwgroup = alloc_hwgroup();  [spinlock_t lock; for request serialization]
                                              |
                                              +--> For select=0 (master) then 1 (slave):
                                                   |
                                                   +--> do_probe(drive, select);  [ide-probe.c: ~line 300]
                                                        |
                                                        +--> unsigned int unit = (hwif->index & 1) ? 0 : select;  [0=primary,1=secondary]
                                                        |     |
                                                        |     +--> drive = kmalloc(sizeof(ide_drive_t), GFP_KERNEL);
                                                        |           |
                                                        |           +--> Init drive: drive->name[0]='h' + 'a' + hwif->index*2 + unit;
                                                        |           |     [e.g., "hda" for hwif0-master]
                                                        |           |     +--> drive->hwif = hwif; drive->select.all=0xA0 | (select<<4);
                                                        |           |     +--> drive->ready_stat = BUSY_STAT; drive->mult_count=0;
                                                        |           |
                                                        |           +--> hwif->drives[select] = drive;
                                                        |
                                                        +--> Select Device: ide_delay_50ms(); outb(drive->select.all, hwif->ctl);  [0x3F6]
                                                        |     |
                                                        |     +--> ide_delay_50ms();  [~50ms wait for drive select]
                                                        |     |     [udelay(50); --loop; ~1000x for 50ms]
                                                        |     |
                                                        |     +--> Timeout? (status & BUSY_STAT): drive->present = 0; kfree(drive); drive=NULL; continue;
                                                        |
                                                        +--> Reset & Wait: outb(WIN_SRST, hwif->io_ports[IDE_COMMAND_OFFSET]);  [soft reset 0x1F7]
                                                        |     |
                                                        |     +--> ide_delay_50ms(); outb(0, hwif->ctl); ide_delay_50ms();
                                                        |     +--> If !(inb(hwif->io_ports[IDE_STATUS_OFFSET]) & BUSY_STAT): Ready.
                                                        |     |     [else: timeout 31s total; printk("hda: reset failed")]
                                                        |
                                                        +--> Send IDENTIFY: outb(WIN_IDENTIFY, hwif->io_ports[IDE_COMMAND_OFFSET]);  [0xEC]
                                                              |
                                                              +--> ide_delay_50ms();  [wait for DRQ or ERR]
                                                              |
                                                              +--> status = inb(hwif->io_ports[IDE_STATUS_OFFSET]);
                                                                    |
                                                                    +--> If (status & (ERR_STAT | DRQ_STAT)) == 0: No device; drive=NULL;
                                                                    |
                                                                    +--> Else: ide_input_data(drive, hwif, id, SECTOR_WORDS);  [read 512B packet]
                                                                          |     [512/2=256 words; insw() from 0x1F0 to id[] buffer]
                                                                          |     |
                                                                          |     +--> Timeout per word? (3s total): drive=NULL; printk("hda: ident timeout")
                                                                          |
                                                                          +--> Valid IDENTIFY? (id[0] != 0xFFFF && !(id[0] & 0x8080))
                                                                                |
                                                                                +--> N (all FF or fixed pattern): drive=NULL; continue;
                                                                                |
                                                                                +--> Y: Parse IDENTIFY Packet (id[0..255] words; ide-probe.c: ~line 100)
                                                                                     |
                                                                                     +--> string_to_ascii(id+54, 40);  [model = "ST380011A" bytes 54-93]
                                                                                     |     |
                                                                                     |     +--> string_to_ascii(id+27, 40);  [serial bytes 27-46]
                                                                                     |
                                                                                     +--> Capacity Calc: If LBA (id[49] & 2): sectors = id[60]<<32 | id[61]<<16 | ... wait, 2.6.20 uses CHS fallback
                                                                                     |     |     [actually: if (id[83] & 0x0400) 48-bit LBA; else 28-bit from id[57-58,60-61]]
                                                                                     |     +--> drive->cyl = id[1]; drive->head = id[3]; drive->sect = id[6];  [CHS]
                                                                                     |     +--> drive->capacity64 = ide_get_capacity(id);  [~MB calc]
                                                                                     |
                                                                                     +--> Media Type: If (id[0] == 0xEB14 || id[0] == 0xC33C): ATAPI (id[0]=ATAPI sig)
                                                                                     |     |     |
                                                                                     |     |     +--> Y: drive->media = ide_cdrom; probe_atapi(drive);  [separate IDENTIFY PACKET cmd 0xA1]
                                                                                     |     |     |     [e.g., for CD: media=ide_cdrom; driver=ide-cd]
                                                                                     |     |     |
                                                                                     |     |     +--> N: drive->media = ide_disk;  [HDD; config=id[0] & 0x00FF == 0x0000? fixed : removable]
                                                                                     |     |
                                                                                     |     +--> Capabilities: If (id[49] & 0x0002): LBA support; drive->using_lba = 1;
                                                                                     |     |     +--> DMA modes: id[63] & 0x04 ? UDMA : id[62] for MWDMA; set drive->dma = 1;
                                                                                     |
                                                                                     +--> alloc/init ide_drive_t (expanded):
                                                                                     |     +--> drive->id = id;  [copy IDENTIFY buffer]
                                                                                     |     +--> drive->present = 1; drive->dead = 0; drive->removable = (id[0]>>14)&1;
                                                                                     |     +--> drive->using_dma = 0;  [tune_dma(drive) later if enabled]
                                                                                     |     +--> drive->mult_count = id[59];  [multi-sector; up to 16]
                                                                                     |     +--> list_add(&drive->list, &ide_drives);  [unbound queue for matching]
                                                                                     |
                                                                                     +--> hwif->drives[select] = drive;
                                                                                     |
                                                                                     +--> If boot param hdx=autotune: ide_tuneproc(drive, 180);  [PIO mode 4 timing]
                                                                                  |
                                                                              +--> ide_probe_ide_device(drive);  [ide.c: ~line 1200; post-probe binding]
                                                                                   |
                                                                                   +--> Scan registered ide_driver_t list (e.g., from ide_disk_init())
                                                                                        |
                                                                                        +--> For each driver in ide_drives_list:
                                                                                             |
                                                                                             +--> Match? (driver->media == drive->media && driver->supports_dsc_overlap)
                                                                                                  |     [e.g., ide_disk_driver.media == ide_disk (0x10)]
                                                                                                  |
                                                                                                  +--> Y: drive->driver = &driver->gen_driver;  [struct device_driver]
                                                                                                  |     |
                                                                                                  |     +--> If driver->probe: driver->probe(drive);  [rare; usually NULL]
                                                                                                  |     |     [e.g., ide_disk_probe: check id[83] for FLUSH cmd support]
                                                                                                  |     |
                                                                                                  |     +--> device_register(&drive->dev);  [/sys/block/hda/ide/]
                                                                                                  |     |     [dev.parent = &hwif->dev; dev.bus = &ide_bus_type]
                                                                                                  |     |
                                                                                                  |     +--> If boot param hdx=drivername: Force match (e.g., hda=ide-disk)
                                                                                                  |
                                                                                                  +--> ide_init_xx(drive)  [driver-specific init; e.g., ide_init_disk() in ide-disk.c: ~line 100]
                                                                                                       |
                                                                                                       +--> If media != ide_disk: return;  [skip non-HDD]
                                                                                                       |
                                                                                                       +--> struct gendisk *gd = alloc_disk(1 << PARTN_BITS);  [max 16/32/64 parts; 2.6.20=16]
                                                                                                       |     |
                                                                                                       |     +--> gd->major = IDE0_MAJOR + (hwif->index >> 1);  [3 for ide0/1]
                                                                                                       |     +--> gd->first_minor = ((hwif->index & 1) * 0x20) + (select << PARTN_BITS);  [64 minors/drive]
                                                                                                       |     +--> gd->fops = &ide_fops;  [block_dev_operations: open=ide_open, ioctl=ide_ioctl]
                                                                                                       |     +--> gd->private_data = drive;
                                                                                                       |     +--> gd->queue = blk_init_queue(do_ide_request, &ide_lock);  [request fn from driver]
                                                                                                       |           |
                                                                                                       |           +--> blk_queue_max_sectors(q, 255);  [from id[59] mult_count or 128]
                                                                                                       |           +--> blk_queue_hardsect_size(q, 512);  [ATA standard]
                                                                                                       |
                                                                                                       +--> Set Disk Info: strcpy(gd->disk_name, drive->name);  ["hda"]
                                                                                                       |     |
                                                                                                       |     +--> set_capacity(gd, drive->capacity64 >> 11);  [MB/1024 for user-visible]
                                                                                                       |     +--> If LBA: gd->real_capacity = ...;  [full sectors]
                                                                                                       |
                                                                                                       +--> Partition Table: ide_read_partition(drive);  [via kernel_read() on sector 0]
                                                                                                       |     |
                                                                                                       |     +--> Timeout? printk("hda: partition table error");  [fallback to whole disk]
                                                                                                       |
                                                                                                       +--> Register Device: add_disk(gd);  [block/ register_blkdev(); creates /dev/hda{1-16}]
                                                                                                       |     |
                                                                                                       |     +--> /sys/block/hda/ (capacity, removable, ro); /sys/bus/ide/devices/hda
                                                                                                       |     +--> proc_ide_create(drive, "ide0/hda", S_IFREG | S_IRUGO, &ide_disk_proc_fops);
                                                                                                       |           |
                                                                                                       |           +--> /proc/ide/hda/identify (hexdump id[]); /proc/ide/hda/settings (tune_dma=1)
                                                                                                       |
                                                                                                       +--> Enable DMA? (if drive->dma && !hwif->dma_base): ide_dma_on(drive);
                                                                                                       |     |     [tune_dma(drive, 255); select UDMA mode from id[88]; setup_dma() allocs channels]
                                                                                                       |     +--> Fail: "hda: DMA disabled" (PIO fallback)
                                                                                                       |
                                                                                                       +--> Quiesce: ide_driveid_update(drive);  [update id[] if needed]
                                                                                                  |
                                                                                                  +--> N (no match): drive->driver = ide_default_driver;  [fallback; basic do_request=ide_do_request]
                                                                                                  |     |
                                                                                                  |     +--> printk("hda: using ide-default");  [limited; no partitions/DMA]
                                                                                                  |     +--> device_register(&drive->dev);  [still exposes in sysfs]
                                                                                  |
                                                                              +--> End Probe for this Drive
                                                                   |
                                                               +--> End For select=0/1 (drives probed)
                                                    |
                                                +--> End For hwif_idx=0-3
                                     |
                                 +--> All Probes Done: ide_scan_hwifs();  [final scan for missed]
                                      |
                                      +--> If ide=debug: printk full hwif/drive dumps
    |
    v
Block Layer Init (device_initcalls: blk_init_queue, register_blkdev)  [fs: block_dev_init]
    |
    +--> All /dev/hda* ready; elevator setup (deadline by default)
    |
    v
Root FS Mount (do_mount_root() if root=/dev/hda1)  [read_super() --> ext2/whatever]
    |
    +--> If no rootdev: Panic "No init found" (but hda probed early)
    |
    v
User Space (init=/sbin/init; pid1 starts)  [mount /proc, /sys; hdparm for tuning]
    |
    +--> User can: hdparm -i /dev/hda (dumps IDENTIFY); mkfs if new disk
