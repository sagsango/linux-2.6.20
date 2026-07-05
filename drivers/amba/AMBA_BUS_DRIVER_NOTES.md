# AMBA (Advanced Microcontroller Bus Architecture) Bus Driver - Comprehensive Technical Notes

## TABLE OF CONTENTS
1. [BACKGROUND](#background)
2. [AMBA OVERVIEW](#amba-overview)
3. [ARCHITECTURE & DESIGN](#architecture--design)
4. [DATA STRUCTURES](#data-structures)
5. [BUS INITIALIZATION](#bus-initialization)
6. [DEVICE REGISTRATION](#device-registration)
7. [DRIVER BINDING](#driver-binding)
8. [DEVICE MATCHING](#device-matching)
9. [PRIMECELLS](#primecells)
10. [INTERRUPT HANDLING](#interrupt-handling)
11. [POWER MANAGEMENT](#power-management)
12. [HOTPLUG SUPPORT](#hotplug-support)

---

## BACKGROUND

### Historical Context
- **Architecture**: ARM-based systems (primarily ARM processors)
- **Standard**: Advanced Microcontroller Bus Architecture (AMBA) specification
- **Devices**: Primecell peripherals (standardized controller design from ARM)
- **Implementation**: Linux kernel support in `drivers/amba/`
- **Kernel Version**: Linux 2.6.20 and later
- **Scope**: Simplified bus layer for ARM-based microcontroller-class peripherals

### What is AMBA?
AMBA is an ARM specification that defines:
1. **Bus Standard**: How peripherals connect to the main processor
2. **Device Identification**: Hardware cell ID and peripheral ID
3. **Register Layout**: Standard offset locations for device-specific registers
4. **Interrupt Model**: How peripherals signal the processor
5. **Configuration**: Resource requirements (memory, I/O, IRQ)

### Primecells
Primecells are standardized ARM-designed peripheral controllers that follow the AMBA specification:
- **DMA Controllers**: Data transfer engines
- **UARTs**: Serial communication
- **Graphics Controllers**: Display interfaces
- **Timers**: Interval/watchdog timers
- **Interrupt Controllers**: ARM PIC
- **Clock Controllers**: System clock management
- **GPIO**: General Purpose I/O

---

## AMBA OVERVIEW

### Purpose
The AMBA bus driver is a Linux kernel subsystem that provides:

1. **Device Discovery**: Find and enumerate AMBA/Primecell devices on the bus
2. **Device Registration**: Integrate AMBA devices with the Linux device model
3. **Driver Binding**: Match drivers to devices based on peripheral ID
4. **Resource Management**: Allocate memory, interrupts, DMA resources
5. **Power Management**: Control device power states (suspend/resume)
6. **Hotplug Support**: Dynamic device insertion/removal (if supported)

### Key Concepts

```
AMBA Bus Layer (drivers/amba/):
    |
    ├─ Device Discovery: Find Primecells on bus
    ├─ Device Registration: Register with Linux device model
    ├─ Driver Matching: Find suitable driver for device
    ├─ Device Binding: Attach driver to device
    └─ Lifecycle Management: Suspend, resume, remove

Benefits:
    ✓ Standardized device format (no BIOS tables, hardware detection)
    ✓ Embedded systems (ARM-based, often no BIOS/UEFI)
    ✓ Simple, lightweight bus layer
    ✓ No firmware/AML required (vs. ACPI)
    ✓ Direct memory-mapped device access
```

### Comparison with Other Bus Types

| Aspect | AMBA | PCI | USB | ACPI |
|--------|------|-----|-----|------|
| **Discovery** | Hardware detection | PCI config space | Device enumeration | Firmware tables/AML |
| **Firmware** | Minimal/none | ROM | Descriptor | BIOS/DSDT |
| **Hotplug** | Optional | Limited | Full | Full |
| **Architecture** | ARM primarily | x86/x64, ARM | Universal | x86/x64, IA64 |
| **Power Mgmt** | Simple | Basic | Full | Full |
| **Interrupt** | Fixed mapping | MSI/INTx | Async | GPE/Fixed |

---

## ARCHITECTURE & DESIGN

### Layered Architecture

```
┌──────────────────────────────────────────────────────────┐
│              Device Drivers                              │
│  (UART driver, DMA controller driver, etc.)              │
│  - Use amba_device and amba_driver structures            │
│  - Register with AMBA bus                                │
└───────────────────┬──────────────────────────────────────┘
                    │
┌───────────────────↓──────────────────────────────────────┐
│              AMBA Bus Driver                              │
│  (drivers/amba/bus.c)                                    │
│  - Device registration                                   │
│  - Driver binding                                        │
│  - Interrupt management                                  │
│  - Power management (suspend/resume)                     │
└───────────────────┬──────────────────────────────────────┘
                    │
┌───────────────────↓──────────────────────────────────────┐
│         Linux Device Model (kernel/device)               │
│  - Generic device/driver framework                       │
│  - sysfs registration                                    │
│  - Power management hooks                                │
│  - Hotplug support                                       │
└───────────────────┬──────────────────────────────────────┘
                    │
┌───────────────────↓──────────────────────────────────────┐
│              Hardware                                    │
│                                                          │
│  ┌─────────────────┐  ┌──────────────┐                  │
│  │ Primecell #1    │  │ Primecell #2 │  ...             │
│  │ (e.g., UART)    │  │ (e.g., DMA)  │                  │
│  └────────┬────────┘  └──────┬───────┘                  │
│           │                   │                         │
│           └───────┬───────────┘                         │
│                   │                                     │
│              AMBA Bus                                    │
│          (address/data/control)                         │
│                   │                                     │
│              ARM CPU Core                                │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

### Design Principles

1. **Simplicity**: Minimal overhead for embedded systems
2. **Standardization**: All AMBA devices follow same structure
3. **Integration**: Deep integration with Linux device model
4. **Portability**: Same driver works across different ARM platforms
5. **Scalability**: Supports multiple AMBA devices on single bus

---

## DATA STRUCTURES

### AMBA Device Structure

```c
struct amba_device {
    struct device dev;          /* Linux device model container */
    struct resource res;        /* Memory resource (base address, size) */
    u64 dma_mask;              /* DMA capability mask */
    unsigned int periphid;      /* Peripheral ID (hardware device ID) */
    unsigned int irq[AMBA_NR_IRQS];  /* Interrupt lines (up to 2) */
};
```

#### Fields Explained

| Field | Purpose | Details |
|-------|---------|---------|
| `dev` | Linux device | Embedded `struct device` for device model |
| `res` | Memory resource | Physical address base and size |
| `dma_mask` | DMA capability | Which addresses device can access (e.g., 32-bit = 0xFFFFFFFF) |
| `periphid` | Hardware ID | Unique identifier read from device registers |
| `irq[0], irq[1]` | Interrupts | IRQ numbers for device (AMBA supports max 2) |

#### Peripheral ID Format (periphid - 32-bit)

```
Bits 31:24 - Configuration (Config)
  └─ Device-specific configuration options

Bits 23:20 - Revision (Rev)
  └─ Hardware revision level

Bits 19:12 - Manufacturer (Manf)
  └─ Who designed the peripheral (e.g., ARM, Philips)

Bits 11:0 - Part Number (Part)
  └─ Which device type (e.g., 0x140 = UART, 0x141 = TIMER)

Extraction Macros:
  amba_config(d) = (periphid >> 24) & 0xFF  /* Configuration */
  amba_rev(d)    = (periphid >> 20) & 0x0F  /* Revision */
  amba_manf(d)   = (periphid >> 12) & 0xFF  /* Manufacturer */
  amba_part(d)   = (periphid & 0xFFF)       /* Part number */

Example: 0x00141011
  - Config = 0x00
  - Rev = 0x0
  - Manf = 0x14
  - Part = 0x011
```

### AMBA Driver Structure

```c
struct amba_driver {
    struct device_driver drv;   /* Linux device driver container */
    
    /* Driver callbacks */
    int (*probe)(struct amba_device *, void *);     /* Claim device */
    int (*remove)(struct amba_device *);            /* Release device */
    void (*shutdown)(struct amba_device *);         /* System shutdown */
    int (*suspend)(struct amba_device *, pm_message_t);   /* Power save */
    int (*resume)(struct amba_device *);            /* Power restore */
    
    struct amba_id *id_table;   /* Table of supported devices */
};
```

#### Driver Callbacks

| Callback | Purpose | Context | Return |
|----------|---------|---------|--------|
| `probe()` | Initialize device | Called when device found | 0=success, <0=error |
| `remove()` | Shutdown device | Called on unload/removal | 0=success |
| `shutdown()` | System shutting down | Pre-poweroff cleanup | void |
| `suspend()` | Enter low-power mode | PM event (S3, S4, etc.) | 0=success |
| `resume()` | Exit low-power mode | Waking from suspend | 0=success |

### AMBA ID Table

```c
struct amba_id {
    unsigned int id;            /* Device ID to match */
    unsigned int mask;          /* Bits to compare (wildcard bits = 0) */
    void *data;                 /* Driver-specific data pointer */
};
```

#### ID Matching Logic

```c
/* Device matches if: (device_periphid & id_table.mask) == id_table.id */

Example 1: Exact match for UART at revision 3
  .id   = 0x00141110   (Manf=0x14, Part=0x110)
  .mask = 0xFFFFFFFF   (All bits must match exactly)
  
  Matches devices with periphid = exactly 0x00141110

Example 2: Match any revision of UART
  .id   = 0x00041110   (Manf=0x04, Part=0x110)
  .mask = 0xFFFFFF00   (Config/Rev can differ, Manf/Part must match)
  
  Matches: 0x00041110, 0x10041110, 0x20041110, etc.
  Does not match: 0x00041111 (Part differs)

Example 3: Match multiple manufacturers' UARTs
  .id   = 0x00000110   (Part=0x110, all others don't care)
  .mask = 0x00000FFF   (Only Part number must match)
  
  Matches any UART from any manufacturer
```

### Helper Macros

```c
#define to_amba_device(d)   container_of(d, struct amba_device, dev)
#define to_amba_driver(d)   container_of(d, struct amba_driver, drv)

#define amba_get_drvdata(d) dev_get_drvdata(&d->dev)
#define amba_set_drvdata(d,p) dev_set_drvdata(&d->dev, p)

/* Peripheral ID accessors */
#define amba_config(d)  (((d)->periphid >> 24) & 0xff)
#define amba_rev(d)     (((d)->periphid >> 20) & 0x0f)
#define amba_manf(d)    (((d)->periphid >> 12) & 0xff)
#define amba_part(d)    ((d)->periphid & 0xfff)
```

---

## BUS INITIALIZATION

### System Boot Sequence

```
1. Kernel boot (arch/arm/kernel/setup.c)
   └─ CPU initialized, memory setup
   
   ↓
   
2. Postcore initialization phase (postcore_initcall)
   └─ amba_init() called
       ├─ bus_register(&amba_bustype)
       └─ AMBA bus infrastructure ready
   
   ↓
   
3. Architecture-specific device registration
   └─ Platform code (arch/arm/mach-xxx/device.c)
       ├─ For each AMBA device on platform:
       │  ├─ Create amba_device structure
       │  ├─ Fill in resource (memory address)
       │  ├─ Set up IRQ numbers
       │  └─ Call amba_device_register()
       │
       └─ Devices now visible in device tree
   
   ↓
   
4. Device driver loading (module_init or built-in)
   └─ For each AMBA driver:
       ├─ Fill amba_id_table with supported devices
       ├─ Call amba_driver_register()
       └─ Driver now probes matching devices
   
   ↓
   
5. Device probe and initialization
   └─ For each device/driver match:
       ├─ Call driver->probe()
       ├─ Driver initializes hardware
       └─ Device operational
```

### Bus Structure

```c
static struct bus_type amba_bustype = {
    .name = "amba",
    .dev_attrs = amba_dev_attrs,    /* sysfs device attributes */
    .match = amba_match,             /* Device/driver matching */
    .uevent = amba_uevent,           /* Hotplug notifications */
    .suspend = amba_suspend,         /* Power suspend callback */
    .resume = amba_resume,           /* Power resume callback */
};
```

#### Bus Initialization Function

```c
static int __init amba_init(void)
{
    return bus_register(&amba_bustype);
}

postcore_initcall(amba_init);
```

**Timing**: Runs during `postcore_initcall` phase (very early in boot, before modules)

**Effect**: Registers "amba" bus type with Linux device model
  - Creates `/sys/bus/amba/`
  - Enables device registration
  - Enables driver registration

---

## DEVICE REGISTRATION

### Device Registration Flow

```
Platform Code calls:
  amba_device_register(&device, &parent_resource)
  
Step 1: Initialize Device Structure
  dev->dev.release = amba_device_release    /* Cleanup callback */
  dev->dev.bus = &amba_bustype              /* Attach to AMBA bus */
  dev->dev.dma_mask = &dev->dma_mask        /* DMA capability */
  dev->res.name = dev->dev.bus_id           /* Name resource after device */
  
Step 2: Claim Memory Resource
  request_resource(parent, &dev->res)
  
  Purpose:
    - Register memory range with resource manager
    - Prevent other drivers from using same memory
    - Validate range is available
  
  Example:
    device->res.start = 0x10001000   (Base address)
    device->res.end   = 0x10001FFF   (Size: 4KB)
    device->res.flags = IORESOURCE_MEM | IORESOURCE_BUSY

Step 3: Read Peripheral ID from Device
  tmp = ioremap(dev->res.start, SZ_4K)
        └─ Map device memory into CPU address space
  
  for (i = 0; i < 4; i++)
    pid |= (readl(tmp + 0xfe0 + 4*i) & 255) << (i*8)
           └─ Read PID0-PID3 registers at offset 0xfe0-0xfec
  
  for (i = 0; i < 4; i++)
    cid |= (readl(tmp + 0xff0 + 4*i) & 255) << (i*8)
           └─ Read CID0-CID3 registers at offset 0xff0-0xffc
           └─ Magic number: 0xB105F00D indicates valid Primecell

Step 4: Validate Primecell
  if (cid == 0xb105f00d)
    dev->periphid = pid
  
  if (!dev->periphid) {
    return -ENODEV  /* Not a valid Primecell */
  }

Step 5: Register with Linux Device Model
  device_register(&dev->dev)
  
  Creates:
    /sys/bus/amba/devices/{bus_id}/
    /sys/devices/{path_to_device}/
  
  Files:
    id           - Peripheral ID (0xHHHHHHHH)
    resource     - Memory address range
    irq0, irq1   - Interrupt numbers (if present)
    driver       - Attached driver name (after probe)
    uevent       - Hotplug event trigger

Step 6: Create IRQ Attributes
  if (dev->irq[0] != NO_IRQ)
    device_create_file(&dev->dev, &dev_attr_irq0)
  
  if (dev->irq[1] != NO_IRQ)
    device_create_file(&dev->dev, &dev_attr_irq1)
    
  Creates /sys files:
    irq0, irq1 - Show IRQ numbers in sysfs
    
Return: 0 if success, error code otherwise
```

### Memory Layout - Primecell Identification Registers

```
Primecell peripherals have standard identification registers at fixed offsets:

Base Address + 0xFE0: PID0 (Peripheral ID 0)
Base Address + 0xFE4: PID1 (Peripheral ID 1)
Base Address + 0xFE8: PID2 (Peripheral ID 2)
Base Address + 0xFEC: PID3 (Peripheral ID 3)
  └─ Combined: 32-bit Peripheral ID

Base Address + 0xFF0: CID0 (Component ID 0)
Base Address + 0xFF4: CID1 (Component ID 1)
Base Address + 0xFF8: CID2 (Component ID 2)
Base Address + 0xFFC: CID3 (Component ID 3)
  └─ Combined: 32-bit Component ID
  └─ Magic: 0xB105F00D indicates valid Primecell

Example Memory Layout (16KB UART device):

Offset      Content
0x0000      UART data register (DR)
0x0004      UART control register (CR)
...
0x0F00      Device-specific registers
...
0xFE0       PID0 = 0x11
0xFE4       PID1 = 0x10
0xFE8       PID2 = 0x04
0xFEC       PID3 = 0x00
  └─ Peripheral ID = 0x00041011 (UART from ARM)

0xFF0       CID0 = 0x0D
0xFF4       CID1 = 0xF0
0xFF8       CID2 = 0x05
0xFFC       CID3 = 0xB1
  └─ Component ID = 0xB105F00D (Valid Primecell signature)
```

---

## DRIVER BINDING

### Device-Driver Matching Algorithm

```c
static int amba_match(struct device *dev, struct device_driver *drv)
{
    struct amba_device *pcdev = to_amba_device(dev);
    struct amba_driver *pcdrv = to_amba_driver(drv);
    
    return amba_lookup(pcdrv->id_table, pcdev) != NULL;
}

static struct amba_id *
amba_lookup(struct amba_id *table, struct amba_device *dev)
{
    int ret = 0;
    
    while (table->mask) {              /* Iterate until mask == 0 */
        ret = (dev->periphid & table->mask) == table->id;
        if (ret)                       /* Match found */
            break;
        table++;
    }
    
    return ret ? table : NULL;         /* Return matching entry or NULL */
}
```

### Probe Phase

```
When device and driver match:

1. Device Model Calls:
   driver->probe(&device)
   
2. Conversion Layer (amba_probe):
   a) Get AMBA-specific device and driver pointers
   b) Look up matched entry in driver's id_table
   c) Call driver->probe(amba_device, id_data)
   
3. Driver Probe Function Responsibility:
   a) Verify device is functional
   b) Read Peripheral ID
   c) Allocate driver-specific resources
   d) Set up interrupts
   e) Initialize hardware
   f) Register device with subsystem (if applicable)
   g) Store driver private data (via amba_set_drvdata)
   
4. Return Value:
   0 = probe successful, driver owns device
   <0 = probe failed, driver rejects device
   
Example Driver Probe:

int uart_probe(struct amba_device *dev, void *id)
{
    struct uart_port *port;
    int ret;
    
    /* Allocate port structure */
    port = kzalloc(sizeof(*port), GFP_KERNEL);
    if (!port)
        return -ENOMEM;
    
    /* Initialize from AMBA device */
    port->mapbase = dev->res.start;
    port->irq = dev->irq[0];
    port->dev = &dev->dev;
    
    /* Request memory region */
    if (!request_mem_region(port->mapbase, SZ_4K, "uart")) {
        ret = -EBUSY;
        goto out_free;
    }
    
    /* Initialize UART hardware */
    ret = uart_add_one_port(&uart_driver, port);
    if (ret)
        goto out_release;
    
    /* Store driver-private data */
    amba_set_drvdata(dev, port);
    
    dev_info(&dev->dev, "UART probe successful\n");
    return 0;
    
out_release:
    release_mem_region(port->mapbase, SZ_4K);
out_free:
    kfree(port);
    return ret;
}
```

---

## DEVICE MATCHING

### ID Table Matching Examples

```c
/* Example 1: UART Driver */
static struct amba_id uart_ids[] = {
    {
        .id   = 0x00041011,      /* ARM UART */
        .mask = 0xFFFFFFFF,      /* Must match exactly */
        .data = &arm_uart_data,
    },
    {
        .id   = 0x00000000,      /* Terminator (mask=0) */
        .mask = 0,
    },
};

Device with periphid 0x00041011:
  (0x00041011 & 0xFFFFFFFF) == 0x00041011 ✓ MATCH

/* Example 2: DMA Controller (multiple revisions) */
static struct amba_id dma_ids[] = {
    {
        .id   = 0x00141080,      /* DMA Rev 0 */
        .mask = 0xFFFFFFFF,      /* Exact match */
        .data = &dma_rev0_data,
    },
    {
        .id   = 0x00141080,      /* DMA any revision */
        .mask = 0xFFFFFF00,      /* Ignore revision bits */
        .data = &dma_generic_data,
    },
    {
        .id   = 0,
        .mask = 0,
    },
};

Device with periphid 0x10141080 (DMA, Rev 1):
  (0x10141080 & 0xFFFFFFFF) == 0x00141080 ? NO
  (0x10141080 & 0xFFFFFF00) == 0x00141080 ? YES ✓ MATCH (2nd entry)

/* Example 3: Catch-all Driver */
static struct amba_id amba_ids[] = {
    {
        .id   = 0x00000000,      /* Any AMBA device */
        .mask = 0x00000000,      /* All bits ignored (0 & 0 == 0) */
        .data = NULL,
    },
    { 0, 0 }
};

Any device:
  (periphid & 0) == 0 ? YES ✓ ALWAYS MATCHES (catch-all)
```

### Matching Priority

When multiple drivers match a device:
1. First matching entry in each driver's table is checked
2. Multiple drivers may match (first registered wins)
3. No priority mechanism between different drivers
4. Later registrations don't displace earlier ones

---

## PRIMECELLS

### What is a Primecell?

A Primecell is an ARM-designed peripheral controller that:

1. **Follows AMBA specification** - Standard memory layout, interrupts
2. **Has identification registers** - Peripheral ID at offsets 0xFE0-0xFEC
3. **Has component ID** - Magic number 0xB105F00D at 0xFF0-0xFFC
4. **Is memory-mapped** - Accessed via reads/writes to memory addresses
5. **Generates interrupts** - Up to 2 interrupt lines to CPU

### Common Primecell Types

| Name | Purpose | Part # | Notes |
|------|---------|--------|-------|
| UART | Serial communication | 0x110 | Dual TX/RX buffered |
| TIMER | Interval/watchdog | 0x120 | Multiple timers |
| GPIO | Digital I/O | 0x130 | Programmable direction |
| DMA | Data transfer | 0x080 | Multi-channel DMA |
| I2C | Serial bus | 0x150 | Master/slave modes |
| USB | Host/device | 0x200 | Full/high-speed |
| LCD | Display controller | 0x221 | Multiple bit depths |
| RTC | Real-time clock | 0x250 | Alarm support |

### Device Registers

All Primecells have three categories of registers:

**1. Data/Control Registers (0x000 - 0xEFF)**
  - Device-specific functionality
  - Variable layout based on device type
  - UART example: DR, CR, FR, ILPR, IBRD, FBRD, LCR_H, CR, IFLS, IMIS, ICIS, ICR, DMACR

**2. Identification Registers (0xF00 - 0xFDF)**
  - Reserved by ARM for future use
  - Not typically accessed by drivers

**3. Cell ID Registers (0xFE0 - 0xFFF)**
  - Peripheral ID (PID0-PID3): 0xFE0-0xFEC
  - Component ID (CID0-CID3): 0xFF0-0xFFC

---

## INTERRUPT HANDLING

### AMBA Interrupt Model

```
struct amba_device supports up to 2 interrupts:
  irq[0] - Primary interrupt
  irq[1] - Secondary interrupt (optional)

Each interrupt is:
  - Fixed IRQ number (assigned by platform)
  - Edge or level triggered (platform dependent)
  - Handled like standard Linux IRQ
```

### Interrupt Registration in Driver

```c
int amba_uart_probe(struct amba_device *dev, void *data)
{
    struct uart_port *port;
    int ret;
    
    port = kzalloc(sizeof(*port), GFP_KERNEL);
    
    /* Register primary interrupt */
    if (dev->irq[0] != NO_IRQ) {
        ret = request_irq(dev->irq[0], uart_interrupt,
                         IRQF_SHARED, "uart", port);
        if (ret)
            goto error;
        
        port->irq = dev->irq[0];
    }
    
    /* Register secondary interrupt (if available) */
    if (dev->irq[1] != NO_IRQ) {
        ret = request_irq(dev->irq[1], uart_interrupt_error,
                         IRQF_SHARED, "uart-error", port);
        if (ret)
            goto error_release_irq0;
    }
    
    return 0;
    
error_release_irq0:
    if (dev->irq[0] != NO_IRQ)
        free_irq(dev->irq[0], port);
error:
    kfree(port);
    return ret;
}
```

### Interrupt Cleanup

```c
void amba_uart_remove(struct amba_device *dev)
{
    struct uart_port *port = amba_get_drvdata(dev);
    
    /* Release interrupts */
    if (dev->irq[0] != NO_IRQ)
        free_irq(dev->irq[0], port);
    
    if (dev->irq[1] != NO_IRQ)
        free_irq(dev->irq[1], port);
    
    /* Release other resources */
    uart_remove_one_port(&uart_driver, port);
    kfree(port);
}
```

---

## POWER MANAGEMENT

### Suspend/Resume Support

```c
struct amba_driver {
    int (*suspend)(struct amba_device *, pm_message_t);
    int (*resume)(struct amba_device *);
};
```

### Suspend Flow

```
System enters sleep state (S3, S4):
  ↓
PM subsystem calls bus->suspend()
  ↓
amba_suspend() called for each device
  ↓
Calls driver->suspend(device, pm_state)
  ↓
Driver saves state:
  - Current configuration registers
  - Buffer contents (if applicable)
  - Power state for recovery
  ↓
Driver performs power saving:
  - Disable interrupts
  - Clear FIFO buffers
  - Disable clocks (if applicable)
  ↓
Device enters low-power state
```

### Resume Flow

```
System wakes from sleep:
  ↓
PM subsystem calls bus->resume()
  ↓
amba_resume() called for each device
  ↓
Calls driver->resume(device)
  ↓
Driver restores state:
  - Reprogram configuration registers
  - Restore FIFO settings
  - Re-enable interrupts
  ↓
Device operational again
```

### Example Suspend/Resume (UART)

```c
static int amba_uart_suspend(struct amba_device *dev,
                             pm_message_t state)
{
    struct uart_port *port = amba_get_drvdata(dev);
    
    if (!port)
        return 0;
    
    /* Let UART subsystem handle suspend */
    return uart_suspend_port(&uart_driver, port);
}

static int amba_uart_resume(struct amba_device *dev)
{
    struct uart_port *port = amba_get_drvdata(dev);
    
    if (!port)
        return 0;
    
    /* Let UART subsystem handle resume */
    return uart_resume_port(&uart_driver, port);
}
```

---

## HOTPLUG SUPPORT

### Hotplug Event Generation

```
When CONFIG_HOTPLUG enabled:

Device insertion/removal generates uevent:
  ↓
amba_uevent() called
  ↓
Generates environment variable:
  AMBA_ID=0xHHHHHHHH
  ↓
udev receives uevent
  ↓
udev loads rules from /etc/udev/rules.d/
  ↓
Rule matches AMBA_ID
  ↓
udev runs device-specific script/modprobe
  ↓
Driver loaded automatically (if module)
```

### Hotplug Event Handler

```c
#ifdef CONFIG_HOTPLUG
static int amba_uevent(struct device *dev, char **envp,
                       int nr_env, char *buf, int bufsz)
{
    struct amba_device *pcdev = to_amba_device(dev);
    
    if (nr_env < 2)
        return -ENOMEM;
    
    snprintf(buf, bufsz, "AMBA_ID=%08x", pcdev->periphid);
    *envp++ = buf;
    *envp++ = NULL;
    return 0;
}
#else
#define amba_uevent NULL
#endif
```

### Device Hotplug Example

```
AMBA Device Physical Hotplug:

1. User removes UART card from slot
   ├─ Hardware notifies platform
   └─ Platform code calls amba_device_unregister()

2. Device Unregistration:
   device_unregister(&dev->dev)
   ├─ Driver's remove() callback called
   ├─ Driver cleans up resources
   └─ Device freed when refcount reaches 0

3. User inserts new UART card
   ├─ Hardware notifies platform
   └─ Platform code calls amba_device_register()

4. Device Registration:
   amba_device_register(&dev, &parent)
   ├─ Reads new device's peripheral ID
   ├─ Registers with device model
   └─ Matching driver's probe() called

5. Driver Probe:
   ├─ Initializes new device
   └─ Device ready for use
```

---

## PRACTICAL EXAMPLES

### Example 1: Simple AMBA Device Driver

```c
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>

#define MY_DEVICE_ID 0x00041011

static struct amba_id my_ids[] = {
    {
        .id = MY_DEVICE_ID,
        .mask = 0xFFFFFFFF,
        .data = NULL,
    },
    { 0, 0 }
};

static int my_probe(struct amba_device *dev, void *id)
{
    int ret;
    
    dev_info(&dev->dev, "Probing device\n");
    
    /* Request memory region */
    if (!request_mem_region(dev->res.start,
                           resource_size(&dev->res),
                           "mydevice")) {
        return -EBUSY;
    }
    
    /* Request interrupt */
    if (dev->irq[0] != NO_IRQ) {
        ret = request_irq(dev->irq[0], my_interrupt_handler,
                         IRQF_SHARED, "mydevice", dev);
        if (ret) {
            release_mem_region(dev->res.start,
                              resource_size(&dev->res));
            return ret;
        }
    }
    
    /* Store driver data for later access */
    amba_set_drvdata(dev, NULL);  /* Or pointer to driver private data */
    
    return 0;
}

static int my_remove(struct amba_device *dev)
{
    dev_info(&dev->dev, "Removing device\n");
    
    /* Release interrupt */
    if (dev->irq[0] != NO_IRQ)
        free_irq(dev->irq[0], dev);
    
    /* Release memory region */
    release_mem_region(dev->res.start,
                      resource_size(&dev->res));
    
    return 0;
}

static void my_shutdown(struct amba_device *dev)
{
    dev_info(&dev->dev, "Shutting down device\n");
}

static struct amba_driver my_driver = {
    .drv = {
        .name = "mydevice",
    },
    .id_table = my_ids,
    .probe = my_probe,
    .remove = my_remove,
    .shutdown = my_shutdown,
};

static int __init my_init(void)
{
    return amba_driver_register(&my_driver);
}

static void __exit my_exit(void)
{
    amba_driver_unregister(&my_driver);
}

module_init(my_init);
module_exit(my_exit);

MODULE_DESCRIPTION("My AMBA Device Driver");
MODULE_LICENSE("GPL");
```

### Example 2: Platform Device Registration

```c
/* In platform code (arch/arm/mach-xxx/devices.c) */

static struct resource my_uart_resources[] = {
    {
        .start = 0x10001000,     /* Physical base address */
        .end   = 0x10001FFF,     /* Size: 4KB (end = start + size - 1) */
        .flags = IORESOURCE_MEM,
    },
};

static struct amba_device my_uart_device = {
    .dev = {
        .init_name = "uart0",
        .coherent_dma_mask = 0xFFFFFFFF,
    },
    .res = my_uart_resources[0],
    .dma_mask = 0xFFFFFFFF,
    .irq = {85, 0},     /* IRQ 85 for UART data, no secondary IRQ */
};

static int __init my_platform_init(void)
{
    return amba_device_register(&my_uart_device,
                               &iomem_resource);
}

module_init(my_platform_init);
```

---

## ADVANCED TOPICS

### Resource Management

```c
int amba_request_regions(struct amba_device *dev,
                        const char *name)
{
    /* Request both memory region and IRQs */
    
    if (!request_mem_region(dev->res.start,
                           resource_size(&dev->res),
                           name))
        return -EBUSY;
    
    return 0;
}

void amba_release_regions(struct amba_device *dev)
{
    /* Release previously requested resources */
    
    release_mem_region(dev->res.start,
                      resource_size(&dev->res));
}
```

### Device Discovery

```c
struct amba_device *amba_find_device(const char *name,
                                      struct device *parent,
                                      unsigned int id,
                                      unsigned int mask)
{
    /* Find AMBA device by various criteria */
    
    struct find_data d = {
        .dev = NULL,
        .parent = parent,
        .busid = name,
        .id = id,
        .mask = mask,
    };
    
    struct device *dev;
    dev = bus_find_device(&amba_bustype, NULL, &d, amba_find_match);
    
    return dev ? to_amba_device(dev) : NULL;
}
```

### DMA Support

```c
For DMA-capable devices, the dma_mask field specifies:
  - Which address bits device can access
  - Typical values:
    0xFFFFFFFF - 32-bit addressing (4GB max)
    0xFFFFFFFFFFFFFFFF - 64-bit addressing
    
Used by DMA subsystem to allocate buffers:
  dma_alloc_coherent(&dev->dev, size, &dma_addr, GFP_KERNEL)
    └─ Returns memory accessible to both CPU and device
```

---

## DEBUGGING & TROUBLESHOOTING

### Viewing AMBA Devices

```bash
# List all AMBA devices
ls /sys/bus/amba/devices/

# Show device info
cat /sys/bus/amba/devices/amba0/id      # Peripheral ID
cat /sys/bus/amba/devices/amba0/resource  # Memory address
cat /sys/bus/amba/devices/amba0/irq0    # IRQ number

# Kernel messages
dmesg | grep -i amba
dmesg | grep amba_device_register  # Registration messages
```

### Debugging Device Registration

```
Kernel debug output:
  - Set CONFIG_DEBUG_DRIVER
  - Monitor dmesg for device discovery
  - Check /proc/iomem for memory allocations
  
Common Issues:
  1. Peripheral ID not recognized
     └─ Device may not be a Primecell (CID != 0xB105F00D)
  
  2. Memory region in use by another driver
     └─ request_resource() returns -EBUSY
  
  3. No matching driver found
     └─ Device registered but no probe called
     └─ Check id_table entries
  
  4. IRQ conflicts
     └─ Multiple devices using same IRQ
     └─ Consider shared interrupts (IRQF_SHARED)
```

---

## SUMMARY: Key Takeaways

1. **Lightweight Bus Layer**: AMBA provides minimal overhead for embedded systems
2. **Standardized Devices**: Primecells follow predictable register layout
3. **Hardware Detection**: Device ID read from hardware, no firmware needed
4. **Simple Matching**: ID table with mask-based matching (similar to PCI)
5. **Linux Integration**: Deep integration with device model (sysfs, PM, hotplug)
6. **ARM-Focused**: Designed for ARM-based microcontroller systems
7. **Scalability**: Scales from single device to multiple devices per bus
8. **Extensible**: Easy to add new device types via new drivers

---

**Document Version**: 1.0
**Linux Kernel**: 2.6.20
**AMBA Specification**: ARM AMBA specification
**Last Updated**: 2024

