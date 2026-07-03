```c
/*
 * FILE: arch/x86_64/pci/mmconfig.c
 *
 * PURPOSE:
 *   Provide low-level PCI configuration-space access using MMCONFIG.
 *
 * BIG IDEA:
 *   Traditional PCI config space can be accessed through I/O ports
 *   using "type 1" config cycles.
 *
 *   MMCONFIG maps PCI config space into memory, so the kernel can access
 *   PCI config registers using normal MMIO reads/writes.
 *
 * This file:
 *
 *      1. reads ACPI MCFG table
 *      2. maps the MMCONFIG aperture with ioremap_nocache()
 *      3. provides raw PCI read/write operations
 *      4. falls back to legacy type1 access for broken devices
 */


/*
 * ============================================================
 * 1. BACKGROUND: PCI CONFIG SPACE
 * ============================================================
 *
 * Every PCI device has configuration space.
 *
 * It contains:
 *
 *      vendor ID
 *      device ID
 *      command/status register
 *      BARs
 *      interrupt pin/line
 *      PCI capabilities
 *
 *
 * Traditional PCI config space:
 *
 *      256 bytes per function
 *
 * PCI Express extended config space:
 *
 *      4096 bytes per function
 *
 *
 * Device identity:
 *
 *      segment
 *      bus
 *      device
 *      function
 *      register
 *
 *
 * devfn:
 *
 *      device/function encoded together
 *
 *      devfn = device << 3 | function
 */


/*
 * ============================================================
 * 2. LEGACY TYPE1 PCI CONFIG ACCESS
 * ============================================================
 *
 * Old mechanism:
 *
 *      write address to I/O port 0xCF8
 *      read/write data from I/O port 0xCFC
 *
 * Called:
 *
 *      PCI configuration mechanism #1
 *      type1 config access
 *
 *
 * In this file fallback functions are:
 *
 *      pci_conf1_read()
 *      pci_conf1_write()
 */


/*
 * ============================================================
 * 3. MMCONFIG / MCFG
 * ============================================================
 *
 * MMCONFIG = memory-mapped PCI config space.
 *
 * Firmware tells OS where MMCONFIG lives through ACPI MCFG table.
 *
 *
 * ACPI MCFG entry says:
 *
 *      base physical address
 *      PCI segment group
 *      start bus
 *      end bus
 *
 *
 * Then config space address is:
 *
 *      mmcfg_base
 *        + bus    * 1MB
 *        + devfn  * 4KB
 *        + reg
 *
 *
 * Why bus * 1MB?
 *
 *      32 devices per bus
 *      8 functions per device
 *      4096 bytes per function
 *
 *      32 * 8 * 4096 = 1MB
 *
 *
 * Why devfn * 4KB?
 *
 *      each function gets 4096 bytes config space
 */


/*
 * ============================================================
 * 4. CONSTANTS
 * ============================================================
 */

#define MMCONFIG_APER_MIN       (2 * 1024 * 1024)
#define MMCONFIG_APER_MAX       (256 * 1024 * 1024)

/*
 * MMCONFIG aperture can be up to 256MB.
 *
 * For one segment:
 *
 *      256 buses * 1MB per bus = 256MB
 *
 *
 * Minimum checked reservation:
 *
 *      2MB
 *
 * Code checks at least first 2MB is E820 reserved.
 */


#define MAX_CHECK_BUS 16

/*
 * For broken-device detection, only first 16 buses are checked.
 */


/*
 * ============================================================
 * 5. fallback_slots
 * ============================================================
 */

static DECLARE_BITMAP(fallback_slots, 32 * MAX_CHECK_BUS);

/*
 * fallback_slots tracks:
 *
 *      bus/device pairs that cannot use MMCONFIG
 *
 *
 * Index:
 *
 *      32 * bus + slot
 *
 *
 * Example:
 *
 *      bus 0 device 24
 *
 *      index = 32 * 0 + 24
 *
 *
 * If bit is set:
 *
 *      use legacy type1 access for that slot.
 */


/*
 * ============================================================
 * 6. mmcfg_virt
 * ============================================================
 */

struct mmcfg_virt {
        struct acpi_table_mcfg_config *cfg;
        char __iomem *virt;
};

static struct mmcfg_virt *pci_mmcfg_virt;

/*
 * For each MCFG entry:
 *
 *      cfg  -> ACPI MCFG config entry
 *      virt -> kernel virtual address after ioremap_nocache()
 *
 *
 * Example:
 *
 *      MCFG physical base 0xe0000000
 *          |
 *          v
 *      ioremap_nocache()
 *          |
 *          v
 *      virtual base 0xffffc20000000000
 */


/*
 * ============================================================
 * 7. get_virt()
 * ============================================================
 *
 * PURPOSE:
 *   Find mapped MMCONFIG virtual base for a given segment and bus.
 */

static char __iomem *get_virt(unsigned int seg, unsigned bus)
{
        int cfg_num = -1;
        struct acpi_table_mcfg_config *cfg;

        while (1) {
                ++cfg_num;
                if (cfg_num >= pci_mmcfg_config_num)
                        break;

                cfg = pci_mmcfg_virt[cfg_num].cfg;

                if (cfg->pci_segment_group_number != seg)
                        continue;

                if ((cfg->start_bus_number <= bus) &&
                    (cfg->end_bus_number >= bus))
                        return pci_mmcfg_virt[cfg_num].virt;
        }

        /*
         * Broken MCFG workaround:
         *
         * Some BIOS tables list only bus 0-0.
         * Assume it applies to all buses.
         */
        cfg = &pci_mmcfg_config[0];

        if (pci_mmcfg_config_num == 1 &&
            cfg->pci_segment_group_number == 0 &&
            (cfg->start_bus_number | cfg->end_bus_number) == 0)
                return pci_mmcfg_virt[0].virt;

        return NULL;
}

/*
 * Flow:
 *
 *      get_virt(seg, bus)
 *          |
 *          v
 *      search all MCFG entries
 *          |
 *          +-- segment mismatch:
 *          |       continue
 *          |
 *          +-- bus outside range:
 *          |       continue
 *          |
 *          +-- match:
 *                  return mapped virtual base
 *
 *      if no match:
 *          try broken-BIOS single-entry workaround
 *
 *      otherwise:
 *          return NULL
 */


/*
 * ============================================================
 * 8. pci_dev_base()
 * ============================================================
 *
 * PURPOSE:
 *   Compute virtual MMCONFIG base address for one PCI function.
 */

static char __iomem *pci_dev_base(unsigned int seg,
                                  unsigned int bus,
                                  unsigned int devfn)
{
        char __iomem *addr;

        if (seg == 0 && bus < MAX_CHECK_BUS &&
            test_bit(32 * bus + PCI_SLOT(devfn), fallback_slots))
                return NULL;

        addr = get_virt(seg, bus);
        if (!addr)
                return NULL;

        return addr + ((bus << 20) | (devfn << 12));
}

/*
 * Address formula:
 *
 *      function_base =
 *          mmcfg_base
 *          + (bus << 20)
 *          + (devfn << 12)
 *
 *
 * Why:
 *
 *      bus << 20
 *          bus * 1MB
 *
 *      devfn << 12
 *          function * 4KB
 *
 *
 * Then actual register:
 *
 *      function_base + reg
 *
 *
 * If fallback_slots bit is set:
 *
 *      return NULL
 *
 * Caller then falls back to pci_conf1_read/write().
 */


/*
 * ============================================================
 * 9. pci_mmcfg_read()
 * ============================================================
 *
 * PURPOSE:
 *   Raw PCI config read through MMCONFIG.
 */

static int pci_mmcfg_read(unsigned int seg,
                          unsigned int bus,
                          unsigned int devfn,
                          int reg,
                          int len,
                          u32 *value)
{
        char __iomem *addr;

        if (unlikely((bus > 255) || (devfn > 255) || (reg > 4095))) {
                *value = -1;
                return -EINVAL;
        }

        addr = pci_dev_base(seg, bus, devfn);
        if (!addr)
                return pci_conf1_read(seg, bus, devfn, reg, len, value);

        switch (len) {
        case 1:
                *value = readb(addr + reg);
                break;
        case 2:
                *value = readw(addr + reg);
                break;
        case 4:
                *value = readl(addr + reg);
                break;
        }

        return 0;
}

/*
 * Flow:
 *
 *      validate bus/devfn/reg
 *          |
 *          v
 *      compute MMCONFIG virtual address
 *          |
 *          +-- no address:
 *          |       pci_conf1_read()
 *          |
 *          v
 *      read 1/2/4 bytes using MMIO accessor
 *
 *
 * Example:
 *
 *      read vendor/device ID:
 *
 *          bus = 0
 *          devfn = PCI_DEVFN(3, 0)
 *          reg = 0
 *          len = 4
 *
 *      addr =
 *          mmcfg_base + bus*1MB + devfn*4KB + 0
 *
 *      readl(addr)
 */


/*
 * ============================================================
 * 10. pci_mmcfg_write()
 * ============================================================
 *
 * PURPOSE:
 *   Raw PCI config write through MMCONFIG.
 */

static int pci_mmcfg_write(unsigned int seg,
                           unsigned int bus,
                           unsigned int devfn,
                           int reg,
                           int len,
                           u32 value)
{
        char __iomem *addr;

        if (unlikely((bus > 255) || (devfn > 255) || (reg > 4095)))
                return -EINVAL;

        addr = pci_dev_base(seg, bus, devfn);
        if (!addr)
                return pci_conf1_write(seg, bus, devfn, reg, len, value);

        switch (len) {
        case 1:
                writeb(value, addr + reg);
                break;
        case 2:
                writew(value, addr + reg);
                break;
        case 4:
                writel(value, addr + reg);
                break;
        }

        return 0;
}

/*
 * Same as read path, but writes using:
 *
 *      writeb()
 *      writew()
 *      writel()
 */


/*
 * ============================================================
 * 11. pci_raw_ops
 * ============================================================
 */

static struct pci_raw_ops pci_mmcfg = {
        .read  = pci_mmcfg_read,
        .write = pci_mmcfg_write,
};

/*
 * The generic PCI layer uses raw_pci_ops.
 *
 * Later:
 *
 *      raw_pci_ops = &pci_mmcfg;
 *
 * means:
 *
 *      use MMCONFIG for PCI config reads/writes.
 */


/*
 * ============================================================
 * 12. unreachable_devices()
 * ============================================================
 *
 * PURPOSE:
 *   Detect devices that cannot be accessed via MMCONFIG and force
 *   fallback to legacy type1 config access.
 */

static __init void unreachable_devices(void)
{
        int i, k;

        for (k = 0; k < MAX_CHECK_BUS; k++) {
                for (i = 0; i < 32; i++) {
                        u32 val1;
                        char __iomem *addr;

                        pci_conf1_read(0, k, PCI_DEVFN(i, 0), 0, 4, &val1);

                        if (val1 == 0xffffffff)
                                continue;

                        addr = pci_dev_base(0, k, PCI_DEVFN(i, 0));

                        if (addr == NULL || readl(addr) != val1) {
                                set_bit(i + 32 * k, fallback_slots);
                                printk(KERN_NOTICE
                                       "PCI: No mmconfig possible on device %02x:%02x\n",
                                       k, i);
                        }
                }
        }
}

/*
 * Why needed?
 *
 * Some AMD K8 systems have built-in northbridge devices that are only
 * accessible using type1 config access.
 *
 *
 * Detection method:
 *
 *      1. Read config dword 0 using legacy type1.
 *
 *          val1 = vendor/device ID
 *
 *      2. Read same location using MMCONFIG.
 *
 *      3. If values mismatch:
 *
 *          mark this bus/device as fallback.
 *
 *
 * Then future pci_dev_base() returns NULL for that slot.
 *
 * That forces:
 *
 *      pci_conf1_read/write()
 */


/*
 * ============================================================
 * 13. pci_mmcfg_init()
 * ============================================================
 *
 * PURPOSE:
 *   Initialize MMCONFIG PCI config access.
 */

void __init pci_mmcfg_init(int type)
{
        int i;

        if ((pci_probe & PCI_PROBE_MMCONF) == 0)
                return;

        acpi_table_parse(ACPI_MCFG, acpi_parse_mcfg);

        if ((pci_mmcfg_config_num == 0) ||
            (pci_mmcfg_config == NULL) ||
            (pci_mmcfg_config[0].base_address == 0))
                return;

        /*
         * Validate that BIOS reserved MMCONFIG area in E820.
         */
        if (type == 1 &&
            !e820_all_mapped(pci_mmcfg_config[0].base_address,
                             pci_mmcfg_config[0].base_address + MMCONFIG_APER_MIN,
                             E820_RESERVED)) {
                printk(KERN_ERR
                       "PCI: BIOS Bug: MCFG area is not E820-reserved\n");
                printk(KERN_ERR "PCI: Not using MMCONFIG.\n");
                return;
        }

        pci_mmcfg_virt =
                kmalloc(sizeof(*pci_mmcfg_virt) * pci_mmcfg_config_num,
                        GFP_KERNEL);

        if (pci_mmcfg_virt == NULL)
                return;

        for (i = 0; i < pci_mmcfg_config_num; ++i) {
                pci_mmcfg_virt[i].cfg = &pci_mmcfg_config[i];

                pci_mmcfg_virt[i].virt =
                        ioremap_nocache(pci_mmcfg_config[i].base_address,
                                         MMCONFIG_APER_MAX);

                if (!pci_mmcfg_virt[i].virt)
                        return;

                printk(KERN_INFO "PCI: Using MMCONFIG at %x\n",
                       pci_mmcfg_config[i].base_address);
        }

        unreachable_devices();

        raw_pci_ops = &pci_mmcfg;

        pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;
}

/*
 * Init flow:
 *
 *      pci_mmcfg_init()
 *          |
 *          v
 *      is MMCONF probing enabled?
 *          |
 *          +-- no:
 *          |       return
 *          |
 *          v
 *      parse ACPI MCFG table
 *          |
 *          v
 *      any valid MCFG config?
 *          |
 *          +-- no:
 *          |       return
 *          |
 *          v
 *      check MCFG area is E820_RESERVED
 *          |
 *          +-- not reserved:
 *          |       reject MMCONFIG
 *          |
 *          v
 *      allocate pci_mmcfg_virt[]
 *          |
 *          v
 *      for each MCFG entry:
 *          |
 *          v
 *      ioremap_nocache(base, 256MB)
 *          |
 *          v
 *      detect unreachable devices
 *          |
 *          v
 *      raw_pci_ops = &pci_mmcfg
 *          |
 *          v
 *      PCI config access now uses MMCONFIG
 */


/*
 * ============================================================
 * 14. ADDRESS CALCULATION EXAMPLE
 * ============================================================
 *
 * Suppose:
 *
 *      MCFG base = 0xe0000000
 *      bus       = 2
 *      device    = 3
 *      function  = 0
 *      reg       = 0x10
 *
 * devfn:
 *
 *      PCI_DEVFN(3, 0) = 3 << 3 | 0 = 24
 *
 * Address:
 *
 *      0xe0000000
 *      + (2 << 20)
 *      + (24 << 12)
 *      + 0x10
 *
 *      = 0xe0000000
 *      + 0x00200000
 *      + 0x00018000
 *      + 0x10
 *
 *      = 0xe0218010
 *
 * That is the memory-mapped config register address.
 */


/*
 * ============================================================
 * 15. COMPLETE FLOW DIAGRAM
 * ============================================================
 *
 *      BIOS/UEFI
 *          |
 *          v
 *      ACPI MCFG table
 *          |
 *          v
 *      pci_mmcfg_init()
 *          |
 *          v
 *      parse MCFG
 *          |
 *          v
 *      validate E820 reservation
 *          |
 *          v
 *      ioremap_nocache(MMCFG base)
 *          |
 *          v
 *      build pci_mmcfg_virt[]
 *          |
 *          v
 *      detect broken devices
 *          |
 *          v
 *      raw_pci_ops = pci_mmcfg
 *          |
 *          v
 *      PCI config read/write
 *          |
 *          v
 *      pci_mmcfg_read/write()
 *          |
 *          v
 *      readb/readw/readl or writeb/writew/writel
 */


/*
 * ============================================================
 * 16. FINAL MENTAL MODEL
 * ============================================================
 *
 * This file replaces slow/legacy I/O-port PCI config access with
 * memory-mapped config access.
 *
 * It uses ACPI MCFG to find the physical MMCONFIG aperture, maps it
 * uncached into the kernel, verifies broken devices, and installs
 * pci_mmcfg_read/write as the raw PCI config operations.
 *
 *
 * One-line summary:
 *
 *      mmconfig.c initializes and implements PCI configuration-space access
 *      through ACPI MCFG/MMCONFIG, while falling back to legacy type1 access
 *      for broken devices or invalid firmware mappings.
 */
```

