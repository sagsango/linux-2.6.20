# Boot * initialization flow 1
───────────────────────────────────────────────────────────────────────────────
           POWER-ON  →  BIOS  →  Bootloader (GRUB/LILO)  →  vmlinuz + initrd
───────────────────────────────────────────────────────────────────────────────
         |                           |
         |<---------------------------|
         |   jump to kernel entry point (arch/x86/kernel/head.S)
         v
───────────────────────────────────────────────────────────────────────────────
[arch/x86/kernel/head.S]
    |
    +--> setup boot page tables
    +--> detect CPU type, enable protected mode
    +--> set up initial stack, zero BSS
    +--> call start_kernel()
───────────────────────────────────────────────────────────────────────────────
[init/main.c]  start_kernel()
    |
    +--> lockdep_init(), printk banner, disable interrupts
    |
    +--> setup_arch(&command_line)
    |     |
    |     +--> parse BIOS e820 memory map
    |     +--> paging_init()  → setup page tables
    |     +--> early CPU init (GDT/IDT)
    |     +--> setup_boot_APIC_clock()
    |     +--> detect SMP / ACPI tables
    |     +--> reserve bootmem for initrd / cmdline
    |
    +--> setup_per_cpu_areas()
    +--> smp_prepare_boot_cpu()
    |
    +--> sched_init()             [initialize runqueue, task_struct for idle]
    +--> preempt_disable()
    +--> build_all_zonelists()
    +--> page_alloc_init()
    |
    +--> trap_init()              [exceptions: #PF, #GP, etc.]
    +--> rcu_init()
    +--> init_IRQ()               [IDT entries + PIC/APIC init]
    +--> timekeeping_init()
    +--> softirq_init()
    +--> timer_init(), hrtimers_init()
    |
    +--> console_init()           [early console output ready]
    |
    +--> vfs_caches_init_early()
    +--> mem_init()               [free all normal pages]
    +--> kmem_cache_init()        [slab allocator]
    +--> setup_per_cpu_pageset()
    +--> numa_policy_init()
    +--> calibrate_delay()        [loops_per_jiffy]
    +--> fork_init()
    +--> proc_caches_init()
    +--> buffer_init()            [buffer_heads]
    +--> unnamed_dev_init()
    +--> key_init()
    +--> security_init()
    +--> vfs_caches_init()
    +--> radix_tree_init()
    +--> signals_init()
    +--> page_writeback_init()
    +--> proc_root_init()
    +--> taskstats_init_early()
    +--> delayacct_init()
    |
    +--> check_bugs()
    +--> acpi_early_init()
───────────────────────────────────────────────────────────────────────────────
    |
    +--> rest_init()    [non-__init part of main; creates first kernel threads]
           |
           +--> kernel_thread(init, NULL, CLONE_FS|CLONE_SIGHAND)
           |        |
           |        v
           |   (this becomes process 1 in kernel space before exec)
           |
           +--> numa_default_policy()
           +--> unlock_kernel()
           +--> preempt_enable_no_resched()
           +--> schedule()           [run first tasks]
           +--> cpu_idle()           [idle loop on CPU0]
───────────────────────────────────────────────────────────────────────────────
        ↓   (meanwhile PID 1 thread runs →  init()  in kernel space)
───────────────────────────────────────────────────────────────────────────────
[init()  in init/main.c]
    |
    +--> set_cpus_allowed()
    +--> init_pid_ns.child_reaper = current
    +--> cad_pid = task_pid(current)
    |
    +--> smp_prepare_cpus(max_cpus)
    +--> do_pre_smp_initcalls()       [spawn ksoftirqd etc.]
    +--> smp_init()                   [bring up APs if SMP]
    +--> sched_init_smp()
    +--> cpuset_init_smp()
    |
    +--> do_basic_setup()
    |     |
    |     +--> init_workqueues()
    |     +--> usermodehelper_init()
    |     +--> driver_init()
    |     |     |
    |     |     +--> devices_init()          → /sys/devices/
    |     |     +--> buses_init()            → /sys/bus/
    |     |     +--> classes_init()          → /sys/class/
    |     |     +--> firmware_init()         → /sys/firmware/
    |     |     +--> hypervisor_init()       → /sys/hypervisor/
    |     |     +--> platform_bus_init()     [platform devices]
    |     |     +--> system_bus_init()
    |     |     +--> cpu_dev_init(), memory_dev_init()
    |     |     +--> attribute_container_init()
    |     |
    |     +--> do_initcalls()                [executes all __initcall routines]
    |           |
    |           +-->  subsys_initcalls()   ─┐
    |           |    (major subsystems)    │
    |           |                          │
    |           |   Examples:              │
    |           |     • pci_init()         │
    |           |     • ide_init()         │
    |           |     • scsi_init()        │
    |           |     • net_init()         │
    |           |     • tty_init()         │
    |           └──────────────────────────┘
    |
    |     ░░ PCI and Device Discovery ░░
    |       pci_init()
    |          → scans bus 0, config space
    |          → pci_register_driver(piix, via, etc.)
    |          → calls ide_pci_probe() for IDE controllers
    |               → alloc ide_hwif_t (io=0x1F0, irq=14)
    |               → request_region(), enable DMA
    |               → add to ide_hwifs list
    |
    |     ░░ IDE Subsystem Init ░░
    |       ide_init()
    |          → bus_register(&ide_bus_type)
    |          → probe_hwif_init()
    |               → probe ports or PCI BARs
    |               → send IDENTIFY to devices
    |               → fill ide_drive_t ("hda","hdb")
    |               → device_register(&drive->gendev)
    |          → idedisk_init()
    |               → driver_register(&idedisk_driver)
    |               → bind driver<->device
    |               → ide_disk_probe()
    |                    → alloc gendisk
    |                    → set_capacity()
    |                    → add_disk() → /dev/hda
    |
    |     ░░ Other initcalls ░░
    |       • char devices (tty, serial, console)
    |       • block layer setup (blk_init_queue)
    |       • filesystems (ext2_init, proc_init)
    |       • networking stack (sock_init, net_dev_init)
    |
    +--> if (!ramdisk_execute_command)
    |         ramdisk_execute_command="/init"
    |
    +--> if (sys_access("/init") != 0)
    |         prepare_namespace()       [mount rootfs or pivot from initrd]
    |             |
    |             +--> create /dev/root (major/minor)
    |             +--> mount_root()
    |                    → open("/dev/hda1") or NFS root
    |                    → mount ext2/ext3/squashfs
    |             +--> switch_root() / pivot_root()
    |
    +--> free_initmem()
    +--> mark_rodata_ro()
    +--> system_state = SYSTEM_RUNNING
    |
    +--> open("/dev/console", O_RDWR)
    +--> dup(0); dup(0);      [stdin/out/err → console]
    |
    +--> try to execute first userspace program:
    |       run_init_process("/sbin/init")
    |       run_init_process("/etc/init")
    |       run_init_process("/bin/init")
    |       run_init_process("/bin/sh")
    |
    +--> if all fail → panic("No init found")
───────────────────────────────────────────────────────────────────────────────
                  ↓
───────────────────────────────────────────────────────────────────────────────
USERSPACE BEGINS
───────────────────────────────────────────────────────────────────────────────
/sbin/init  (PID 1)
    |
    +--> mount /proc, /sys
    +--> read /etc/inittab
    +--> spawn getty, rc scripts
───────────────────────────────────────────────────────────────────────────────
KERNEL STATE AFTER INIT STARTS:
    - All CPUs online, scheduler active
    - IRQs enabled, timer running
    - Memory zones built, slab caches ready
    - Device model live (/sys/)
    - Buses populated (PCI, IDE, USB)
    - Block devices registered (/dev/hda, /dev/sda)
    - Root filesystem mounted (ext2/ext3/initramfs)
───────────────────────────────────────────────────────────────────────────────




# boot and initizalition flow 2
───────────────────────────────────────────────────────────────────────────────
POWER-ON  →  BIOS/EFI (legacy BIOS assumed here)  →  Bootloader (GRUB/LILO)
───────────────────────────────────────────────────────────────────────────────
    BIOS loads stage2 → selects kernel + (optional) initrd
    |
    +--> Loads compressed kernel (vmlinuz) at load addr
    +--> Passes boot params (setup header, command line, e820 map, video mode)
    |
    v
┌─────────────────────────────────────────────────────────────────────────────┐
│ KERNEL DECOMPRESSOR (arch/x86/boot/compressed/)                             │
│   - Runs in 16/32-bit trampoline, sets up flat 32-bit env to unpack kernel │
│   - Uncompresses vmlinux to high memory                                    │
│   - Jumps to decompressed kernel entry (startup_32 / head.S)               │
└─────────────────────────────────────────────────────────────────────────────┘
    |
    v
┌─────────────────────────────────────────────────────────────────────────────┐
│ HEAD & VERY EARLY ARCH SETUP (arch/x86/kernel/head.S / head32.c)            │
│  • Build early (minimal) page tables (identity map + phys→virt mapping)     │
│  • Set up initial stack, BSS clear                                          │
│  • Switch to protected mode (paging off→on), cache/TSC sanity               │
│  • Early GDT/IDT stub entries                                               │
│  • Jump to start_kernel() (init/main.c)                                     │
└─────────────────────────────────────────────────────────────────────────────┘
    |
    v
┌─────────────────────────────────────────────────────────────────────────────┐
│ start_kernel()  (init/main.c)                                               │
│  [Top-level kernel bring-up, interrupts disabled until init_IRQ()]          │
│                                                                             │
│  ┌──────────────── System description & memory topology ────────────────┐   │
│  │ setup_arch(&command_line) (arch/x86)                                  │   │
│  │  - Parse real-mode params, cmdline (e.g., "root=", "init=", "ide=")   │   │
│  │  - Import BIOS e820 memory map, reserve kernel/bootmem regions        │   │
│  │  - Choose paging model (PAE/no-PAE), finalize early page tables       │   │
│  │  - Early CPU features (CPUID), MTRR type defaults                     │   │
│  │  - ACPI tables mapping (RSDP→RSDT/XSDT) (if enabled)                  │   │
│  │  - APIC/IO-APIC discovery (vs PIC 8259)                               │   │
│  │  - Initrd/initramfs pointers & sizes from boot params                 │   │
│  └───────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  • setup_per_cpu_areas()            [allocate per-CPU data blocks]          │
│  • smp_prepare_boot_cpu()          [CPU0 marked online, per-cpu init]       │
│                                                                             │
│  ┌──────────────── Schedulers / time / IRQs / traps ────────────────────┐   │
│  │ sched_init()                  [runqueue, prio arrays, idle task]     │   │
│  │ rcu_init()                    [RCU structures, callbacks]            │   │
│  │ trap_init()                   [IDT handlers for #PF, #GP, #UD...]    │   │
│  │ init_IRQ()                    [PIC or IO-APIC + vector table]        │   │
│  │ timekeeping_init(), timer_init(), hrtimers_init()                    │   │
│  │ softirq_init(), tasklet init                                        │   │
│  └───────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  ┌──────────────── Memory allocators & caches ──────────────────────────┐   │
│  │ build_all_zonelists()         [DMA/Normal/HighMem node/zone lists]   │   │
│  │ page_alloc_init()             [buddy allocator bootstrap]            │   │
│  │ mem_init()                    [free pages to allocator]              │   │
│  │ kmem_cache_init()             [SLAB caches, slab descriptor caches]  │   │
│  │ vfs_caches_init_early()       [dentry/icache scaffolding]            │   │
│  │ vfs_caches_init()             [full dcache/icache]                   │   │
│  │ buffer_init(), page_writeback_init()                                 │   │
│  └───────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  • console_init()                  [early consoles→tty/serial if configured] │
│  • calibrate_delay()               [loops_per_jiffy for busy-waits]         │
│  • security_init()                 [LSM hooks; SELinux if built]            │
│  • key_init(), signals_init(), proc_root_init(), radix_tree_init()          │
│  • check_bugs()                    [CPU errata messaging]                   │
│                                                                             │
│  → rest_init()                                                                │
└─────────────────────────────────────────────────────────────────────────────┘
    |
    v
┌─────────────────────────────────────────────────────────────────────────────┐
│ rest_init()  (init/main.c)                                                  │
│  - Create the first kernel thread:                                          │
│      kernel_thread(init, NULL, CLONE_FS | CLONE_SIGHAND | CLONE_VM)         │
│      (this is still kernel mode; will exec userspace later)                │
│  - Set default NUMA policy                                                 │
│  - Enable preemption & schedule()                                          │
│  - Enter cpu_idle() loop on CPU0                                           │
└─────────────────────────────────────────────────────────────────────────────┘
    |
    v
┌─────────────────────────────────────────────────────────────────────────────┐
│ init()  (the kernel thread that will exec /sbin/init)                       │
│  • Set itself as child_reaper for PID namespace                             │
│  • smp_prepare_cpus()                 [final APIC/IO-APIC prep]             │
│  • do_pre_smp_initcalls()             [ksoftirqd, early kthreads]           │
│  • smp_init()                         [bring up APs; IPI, TSC sync]         │
│  • sched_init_smp(), cpuset_init_smp()                                      │
│  • do_basic_setup():                                                        │
│      ┌──────────────────────────── driver_init() ───────────────────────┐    │
│      │ devices_init()        → builds /sys/devices                      │    │
│      │ buses_init()          → builds /sys/bus + driver core            │    │
│      │ classes_init()        → builds /sys/class                        │    │
│      │ firmware_init(), hypervisor_init()                               │    │
│      │ platform_bus_init(), system_bus_init()                           │    │
│      │ cpu_dev_init(), memory_dev_init()                                │    │
│      │ attribute_container_init()                                       │    │
│      └──────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│      ┌──────────────────────────── do_initcalls() ──────────────────────┐    │
│      │ Execute all initcall levels in order:                            │    │
│      │   early → core → postcore → arch → subsys → fs → device → late  │    │
│      │                                                                  │    │
│      │ [Major highlights you’ll care about:]                            │    │
│      │  • PCI init (drivers/pci/)                                       │    │
│      │      - Scan PCI roots/buses, enumerate functions                 │    │
│      │      - pci_register_driver() for host/storage/net/etc            │    │
│      │      - MSI capability if enabled                                 │    │
│      │  • Block layer init                                              │    │
│      │      - elevator (deadline/anticipatory), request_queues          │    │
│      │      - kblockd kernel thread                                     │    │
│      │  • IDE core + drivers (drivers/ide/)                             │    │
│      │      - ide_init(): registers bus_type "ide"                      │    │
│      │      - Probe controllers: PCI (piix, via, sis, …) or legacy PIO  │    │
│      │      - Build ide_hwif_t (I/O BARs, IRQ, DMA capability)          │    │
│      │      - Send IDENTIFY to (hda/hdb/hdc/hdd), create ide_drive_t    │    │
│      │      - driver_register(&idedisk_driver)                           │    │
│      │      - ide_disk_probe(): alloc gendisk, set_capacity, add_disk   │    │
│      │        → /dev/hda*, /sys/block/hda, /proc/ide/hda/*              │    │
│      │  • libata/SATA (if built), SCSI midlayer, CD-ROM, USB            │    │
│      │  • TTY/console, serial, input (serio), ps/2, framebuffer         │    │
│      │  • Networking core (sock_init, net_dev_init), loopback device    │    │
│      │  • Filesystems register (ext2/ext3/ramfs/tmpfs, proc, sysfs)     │    │
│      └──────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  • initrd / initramfs handling (if provided):                                │
│      - unpack_to_rootfs (early) or pivot in prepare_namespace()              │
│      - runs /init from initramfs if present                                  │
│                                                                              │
│  • prepare_namespace()  (when falling back to real root=)                    │
│      - Create /dev/root (MAJOR/MINOR from root=…)                            │
│      - mount_root():                                                         │
│          * For block root: open /dev/hdaX (or sdaX)                          │
│            → read superblock → mount_fs (ext2/ext3/reiser, etc.)             │
│          * For NFS root: start net, mount nfs                                │
│      - If using initrd: pivot_root/switch_root into real root                │
│                                                                              │
│  • free_initmem()             [discard __init text/data]                      │
│  • mark_rodata_ro()           [final W^X hardening for rodata]                │
│  • system_state = SYSTEM_RUNNING                                             │
│                                                                              │
│  • Open console: open("/dev/console", O_RDWR); dup(0); dup(0)                │
│  • Try userspace init in order:                                              │
│      run_init_process("/sbin/init")                                          │
│      run_init_process("/etc/init")                                           │
│      run_init_process("/bin/init")                                           │
│      run_init_process("/bin/sh")                                             │
│    → if all fail: panic("No init found.")                                    │
└─────────────────────────────────────────────────────────────────────────────┘
    |
    v
┌─────────────────────────────────────────────────────────────────────────────┐
│ FIRST USERSPACE INSTRUCTION EXECUTES (PID 1: /sbin/init)                     │
│  - Mount /proc and /sys                                                      │
│  - Parse inittab/runlevel scripts                                            │
│  - Spawn getty, udev/hotplug helpers, daemons                                │
└─────────────────────────────────────────────────────────────────────────────┘




# sub flow
driver_init()
  ├─ devices_init()    → creates /sys/devices and root kset
  ├─ buses_init()      → /sys/bus; registers bus_type core
  ├─ classes_init()    → /sys/class (block, net, tty, input, ...)
  ├─ firmware_init()   → /sys/firmware (ACPI, DMI, etc.)
  ├─ hypervisor_init() → /sys/hypervisor (if any)
  ├─ platform_bus_init() → platform_bus_type
  ├─ system_bus_init(), cpu_dev_init(), memory_dev_init()
  └─ attribute_container_init()

bus_add_driver(drv)
  ├─ links driver kobject under /sys/bus/<bus>/drivers/<name>
  ├─ iterates existing devices on that bus → match() → probe()
  └─ future devices hotplug → will try to bind to this driver

#  Driver Model spine (why /sys shows what it shows)
subsys_initcall(pci_init)
  └─ Enumerate buses/devs
     └─ pci_register_driver( piix_ide_driver )
        └─ piix_ide_probe(pdev)
           ├─ pci_enable_device()
           ├─ pci_request_regions()
           ├─ pci_set_master()
           ├─ get BARs (I/O ports), IRQ line
           └─ ide_register_hw(&hw_regs, &hwif)
                ├─ ide_hwif_t setup: io_ports[], irq, dma_base, chipset
                ├─ add to ide_hwifs[], request_region()
                └─ probe_hwif_init()
                    ├─ softreset/select, IDENTIFY
                    ├─ build ide_drive_t for each unit found
                    └─ device_register(&drive->gendev) (bus=ide)
subsys_initcall(ide_init)
  ├─ bus_register(&ide_bus_type)
  ├─ driver_register(&idedisk_driver)
  └─ generic match: media==ide_disk → ide_disk_probe()
        ├─ alloc gendisk, queue, minors, disk_name ("hd[a-d]")
        ├─ compute capacity (28/48-bit LBA, HPA handling)
        ├─ blk_queue params, set_capacity()
        └─ add_disk() → /dev/hdaN + /sys/block/hda

# Driver Model spine (why /sys shows what it shows)
subsys_initcall(pci_init)
  └─ Enumerate buses/devs
     └─ pci_register_driver( piix_ide_driver )
        └─ piix_ide_probe(pdev)
           ├─ pci_enable_device()
           ├─ pci_request_regions()
           ├─ pci_set_master()
           ├─ get BARs (I/O ports), IRQ line
           └─ ide_register_hw(&hw_regs, &hwif)
                ├─ ide_hwif_t setup: io_ports[], irq, dma_base, chipset
                ├─ add to ide_hwifs[], request_region()
                └─ probe_hwif_init()
                    ├─ softreset/select, IDENTIFY
                    ├─ build ide_drive_t for each unit found
                    └─ device_register(&drive->gendev) (bus=ide)
subsys_initcall(ide_init)
  ├─ bus_register(&ide_bus_type)
  ├─ driver_register(&idedisk_driver)
  └─ generic match: media==ide_disk → ide_disk_probe()
        ├─ alloc gendisk, queue, minors, disk_name ("hd[a-d]")
        ├─ compute capacity (28/48-bit LBA, HPA handling)
        ├─ blk_queue params, set_capacity()
        └─ add_disk() → /dev/hdaN + /sys/block/hda

# core kernel kthread born before userspace
• ksoftirqd/N          (one per CPU)
• kblockd/0            (block layer)
• pdflush (writeback)  (2+ instances)
• kswapd               (page reclaim)
• keventd / khelper    (usermodehelper runner)
• khubd                (USB hub, if USB)
• kjournald            (if ext3/jbd)



# root mount decision tree
IF initramfs present:
   - unpack initramfs cpio into tmpfs "rootfs"
   - exec /init (userspace) → can pivot to real root later
ELSE:
   - prepare_namespace():
       + parse root= (e.g., root=/dev/hda1, root=UUID=..., root=nfs)
       + create /dev/root with major/minor
       + mount_root() → mount filesystem on /dev/root
       + if initrd: pivot_root/switch_root to real /


# interrups selection
- If IO-APIC present & enabled: init IO-APIC, assign vector IRQs, enable APIC mode
- Else: legacy PIC 8259 (IRQ0..15), IDE usually on 14/15
- Local APIC: calibrate timers (TSC/APIC), set per-CPU vectors (IPIs)
