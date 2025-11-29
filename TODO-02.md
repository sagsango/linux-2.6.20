# start_kernel:
    vfs_cache
        dentry_cache: hash(parent_dentry, name)
        inode_cache: hash(superblock, inode)
    kernel_thread:
        presnet in entry.S
        calls do_fork()
    kernel_execve
        present in the entry.S
        calls do_execve
    init process
        boot cpu
        sched_init : first create task_struct for the ideal task
        after boot  logic is done switches to schsuler -> ideal function
    init (ideal_thread) kernel thread
        task_struct creation
        cpu_ideal : logic execution
    
    start_kernel() -> setup_arch() 
        flaght memory mapping init
        parse efi table for diff devices
        reserver the resource (io-ports, mem-io-regions etc)


# arch_init
──────────────────────────────────────────────────────────────
              ACPI EARLY INIT FLOW (Linux 2.6.20)
──────────────────────────────────────────────────────────────
Firmware / BIOS
   │
   ▼
[ Bootloader passes control to kernel ]
   │
   ▼
setup_arch()
   │
   └── acpi_early_init()
          │
          ├── acpi_boot_init()
          │     ├── find RSDP ("RSD PTR ")
          │     ├── map RSDT/XSDT
          │     ├── verify checksums
          │     ├── parse MADT (APIC)
          │     ├── parse FADT (power mgmt)
          │     ├── parse SRAT (NUMA)
          │     └── enable HPET if present
          │
          ├── acpi_reserve_bootmem()
          │     └── reserve physical memory for ACPI tables
          │
          └── return 0 → continue kernel init
──────────────────────────────────────────────────────────────
Later in boot:
   │
   └── acpi_init()
         ├── build ACPI namespace (DSDT/SSDT)
         ├── enumerate ACPI devices
         ├── enable SCI interrupt
         └── register ACPI power/thermal drivers
──────────────────────────────────────────────────────────────




# Early flat mapping
Virtual address  =  Physical address + PAGE_OFFSET
PAGE_OFFSET = 0xC0000000   (for 3G/1G split kernels)

start_kernel()
  └─> setup_arch()
        └─> paging_init()
              └─> init_memory_mapping(start, end)
──────────────────────────────────────────────────────────────
        KERNEL DIRECT MAPPING INITIALIZATION (x86_64/PAE)
──────────────────────────────────────────────────────────────

Physical memory:
    0x00000000 ──────────────► end_of_ram_pfn
           │
           ▼
    allocate early page tables (find_early_table_space)
           │
           ▼
    ┌─────────────────────────────────────────────────────┐
    │   Kernel virtual memory (starting at PAGE_OFFSET)   │
    │                                                     │
    │  [PGD] → [PUD] → [PMD] → [PTE] → physical pages     │
    │                                                     │
    └─────────────────────────────────────────────────────┘
           │
           ▼
  Each level initialized by:
       phys_pud_init() → phys_pmd_init() → phys_pte_init()
──────────────────────────────────────────────────────────────



# Firmware table
/*
 * All runtime access to EFI goes through this structure:
 */
extern struct efi {
    efi_system_table_t *systab; /* EFI system table */
    unsigned long mps;      /* MPS table */
    unsigned long acpi;     /* ACPI table  (IA64 ext 0.71) */
    unsigned long acpi20;       /* ACPI table  (ACPI 2.0) */
    unsigned long smbios;       /* SM BIOS table */
    unsigned long sal_systab;   /* SAL system table */
    unsigned long boot_info;    /* boot info table */
    unsigned long hcdp;     /* HCDP table */
    unsigned long uga;      /* UGA table */
    efi_get_time_t *get_time;
    efi_set_time_t *set_time;
    efi_get_wakeup_time_t *get_wakeup_time;
    efi_set_wakeup_time_t *set_wakeup_time;
    efi_get_variable_t *get_variable;
    efi_get_next_variable_t *get_next_variable;
    efi_set_variable_t *set_variable;
    efi_get_next_high_mono_count_t *get_next_high_mono_count;
    efi_reset_system_t *reset_system;
    efi_set_virtual_address_map_t *set_virtual_address_map;
} efi;


EFI (root table)
  ├──> ACPI tables (RSDP → RSDT/XSDT)
  │       ├──> MADT (APIC info)
  │       ├──> FADT, DSDT, SSDT, etc.
  │       └──> SRAT, SLIT (NUMA info)
  └──> SMBIOS (DMI tables)

──────────────────────────────────────────────────────────────
              FIRMWARE → KERNEL TABLE RELATIONSHIP
──────────────────────────────────────────────────────────────

EFI SYSTEM TABLE
   │
   ├── ACPI 2.0 Table GUID  ───► RSDP (ACPI entry)
   │                               │
   │                               ▼
   │                      ACPI TABLES (RSDT/XSDT)
   │                           ├─ FADT  → power mgmt regs
   │                           ├─ DSDT  → AML (device tree)
   │                           ├─ SSDT  → CPU / power data
   │                           ├─ MADT  → APIC topology
   │                           ├─ HPET  → timers
   │                           ├─ MCFG  → PCIe ECAM
   │                           ├─ SRAT  → NUMA mapping
   │                           └─ SLIT  → NUMA distances
   │
   ├── SMBIOS Table GUID ──────► DMI tables
   │                               ├─ System info
   │                               ├─ Board info
   │                               └─ Memory, CPU, chassis
   │
   └── MPS Table GUID (legacy) ─► MP Configuration (APICs)
──────────────────────────────────────────────────────────────

──────────────────────────────────────────────────────────────
           DMI / SMBIOS TABLE DISCOVERY FLOW
──────────────────────────────────────────────────────────────
       ┌────────────────────────────────────┐
       │  Firmware (BIOS / EFI)             │
       │   - SMBIOS Entry Point             │
       │   - DMI Tables (structures)        │
       └────────────────────────────────────┘
                      │
                      ▼
           Kernel core_initcall → dmi_scan_machine()
                      │
          ┌───────────┴──────────────────────────┐
          │                                      │
          ▼                                      ▼
     [ EFI boot ]                         [ Legacy BIOS boot ]
      efi.smbios addr                       Scan 0xF0000–0xFFFFF
          │                                      │
          ▼                                      ▼
  dmi_ioremap(addr, 32)                 dmi_ioremap(0xF0000, 64 KB)
          │                                      │
          ▼                                      ▼
     dmi_present(ptr+0x10)                for every 16 bytes:
          │                                       ├─ check "_DMI_"
          │                                       └─ stop when found
          ▼
   store DMI table address
          │
          ▼
   dmi_iounmap()
          │
          ▼
   Later: dmi_decode() → populate /sys/class/dmi/id/
──────────────────────────────────────────────────────────────

# Efi table passing (UEFI mode)
──────────────────────────────────────────────────────────────
       FIRMWARE → BOOTLOADER → KERNEL TABLE HANDOFF FLOW
──────────────────────────────────────────────────────────────

[ FIRMWARE (UEFI) ]
   │
   │ Build all tables:
   │   - ACPI (RSDP→RSDT/XSDT→MADT,FADT,DSDT,SSDT,SRAT,…)
   │   - SMBIOS (DMI)
   │   - EFI System Table with GUIDs to all above
   │
   │ Store addresses in EFI System Table
   │
   ▼
[ BOOTLOADER (GRUB EFI) ]
   │
   │ Load kernel into memory
   │ Retrieve EFI System Table pointer
   │ Fill boot_params.efi_info struct
   │ Pass EFI System Table physical address to kernel (rsi)
   │
   ▼
[ LINUX KERNEL ]
   │
   ├──> efi_init() → copies EFI System Table, saves GUIDs
   │
   ├──> acpi_early_init()
   │       └── uses efi.acpi20 (RSDP pointer) → parse ACPI tables
   │
   ├──> dmi_scan_machine()
   │       └── uses efi.smbios (SMBIOS address) → parse DMI tables
   │
   └──> acpi_table_parse_madt() → build APIC topology
──────────────────────────────────────────────────────────────

# Efi table passing (BIOS, non UEFI mode)
──────────────────────────────────────────────────────────────
            BIOS BOOT TABLE DISCOVERY FLOW (Non-EFI)
──────────────────────────────────────────────────────────────

[ BIOS Firmware ]
   │
   ├─ Builds tables in low memory (below 1 MB)
   │     - RSDP at 0xE0000–0xFFFFF or EBDA
   │     - _DMI_ table at 0xF0000–0xFFFFF
   │     - _MP_ table at 0xF0000–0xFFFFF
   │
   └─ Provides memory map via int 0x15 E820
   │
   ▼
[ Bootloader (GRUB legacy) ]
   │
   ├─ Calls BIOS int 0x15 E820 → fills boot_params.e820
   ├─ Loads kernel (setup header + bzImage)
   └─ Jumps to kernel real-mode entry
   │
   ▼
[ Linux Kernel Early Boot ]
   │
   ├─ e820_register_memory()
   ├─ acpi_find_root_pointer() → scan 0xE0000–0xFFFFF for "RSD PTR "
   ├─ dmi_scan_machine() → scan 0xF0000–0xFFFFF for "_DMI_"
   ├─ smp_scan_config() → scan 0xF0000–0xFFFFF for "_MP_"
   └─ Build internal structures
──────────────────────────────────────────────────────────────


# resource allocation (io-port, memory-io, etc)

setup_arch()
{

    ...
    ...
    ...
    /*
     * Request address space for all standard RAM and ROM resources
     * and also for regions reported as reserved by the e820.
     */
    probe_roms();
    e820_reserve_resources();
    e820_mark_nosave_regions();

    request_resource(&iomem_resource, &video_ram_resource);

    {
    unsigned i;
    /* request I/O space for devices used on all i[345]86 PCs */
    for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
        request_resource(&ioport_resource, &standard_io_resources[i]);
    }
    ...
    ...

}
