# ACPI (Advanced Configuration and Power Interface) Subsystem - Comprehensive Technical Notes

## TABLE OF CONTENTS
1. [BACKGROUND](#background)
2. [ACPI OVERVIEW](#acpi-overview)
3. [ARCHITECTURE & LAYERS](#architecture--layers)
4. [CORE COMPONENTS](#core-components)
5. [AML EXECUTION ENGINE](#aml-execution-engine)
6. [DEVICE INTEGRATION](#device-integration)
7. [POWER MANAGEMENT](#power-management)
8. [THERMAL MANAGEMENT](#thermal-management)
9. [EVENT HANDLING](#event-handling)
10. [DATA FLOW & INITIALIZATION](#data-flow--initialization)
11. [KEY SUBSYSTEMS](#key-subsystems)
12. [DEVICE DRIVERS](#device-drivers)

---

## BACKGROUND

### Historical Context
- **Origin**: ACPI (Advanced Configuration and Power Interface) specification co-developed by Compaq, Intel, Microsoft, Phoenix, and Toshiba
- **Specification**: Industry standard for system configuration and power management
- **Linux Implementation**: Based on Intel's ACPI Component Architecture (ACPI CA)
- **Replaces**: Legacy interfaces like PnP BIOS, MultiProcessor Specification (MPS), Advanced Power Management (APM)
- **Kernel Version**: Linux 2.6.20 with integrated ACPI CA implementation

### Advantages Over Legacy Systems
- **Unified Interface**: Single standard for all vendors (replaces BIOS-specific code)
- **OS-Directed Power Management (OSPM)**: Operating system has control over power states
- **Hotplug Support**: Dynamic device insertion/removal without reboot
- **Flexible Configuration**: Firmware provides device descriptions in tables/AML code
- **Advanced Features**: Device power states, thermal management, system events

### Limitations & Considerations
- **Firmware Quality**: Dependent on BIOS implementation (inconsistent across vendors)
- **Debugging Challenges**: BIOS code (AML) is bytecode - hard to debug
- **Performance Impact**: ~70KB kernel size addition
- **Compatibility**: Not all legacy hardware supports ACPI (APM fallback available)

---

## ACPI OVERVIEW

### Purpose
ACPI is a comprehensive system configuration and power management interface that allows the operating system to:

1. **Discover Devices**: Identify all hardware devices via ACPI tables
2. **Configure Devices**: Set up device parameters, resources, and capabilities
3. **Manage Power**: Control device and system power states
4. **Handle Events**: Respond to hardware events (button press, lid open, etc.)
5. **Manage Thermal**: Monitor temperature and control cooling
6. **Control Processors**: Manage CPU power states and frequency scaling

### High-Level Architecture

```
┌────────────────────────────────────────────────────────────┐
│                  Operating System                          │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │  Kernel      │  │  Drivers     │  │  Applications│    │
│  │  Subsystems  │  │  (PCI, USB,  │  │              │    │
│  │              │  │   etc.)      │  │              │    │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘    │
│         │                  │                  │            │
│         └──────────────────┼──────────────────┘            │
│                            │                               │
└────────────────────────────┼───────────────────────────────┘
                             │ (Power, thermal, events)
                             ↓
┌────────────────────────────────────────────────────────────┐
│              ACPI Subsystem (drivers/acpi)                 │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐ │
│  │         ACPI Bus Driver (bus.c, scan.c)              │ │
│  │  - Device enumeration                                 │ │
│  │  - Device tree management                             │ │
│  │  - Power state transitions                            │ │
│  │  - Event dispatching                                  │ │
│  └──────────────────────────────────────────────────────┘ │
│                            │                               │
│  ┌──────────────────────────────────────────────────────┐ │
│  │  ACPI CA (Component Architecture)                     │ │
│  │  ┌──────────┐ ┌───────────┐ ┌──────────┐            │ │
│  │  │ Parser   │ │ Namespace │ │ Executer │            │ │
│  │  │(AML code)│ │(ACPI tree)│ │(VM for   │            │ │
│  │  │ parsing  │ │ management│ │ AML)     │            │ │
│  │  └──────────┘ └───────────┘ └──────────┘            │ │
│  │  ┌──────────┐ ┌───────────┐ ┌──────────┐            │ │
│  │  │Dispatcher│ │ Resources │ │ Hardware │            │ │
│  │  │(method   │ │ management│ │ interface│            │ │
│  │  │dispatch) │ │(IRQ, I/O) │ │          │            │ │
│  │  └──────────┘ └───────────┘ └──────────┘            │ │
│  └──────────────────────────────────────────────────────┘ │
│                            │                               │
│  ┌──────────────────────────────────────────────────────┐ │
│  │         Device Drivers                                │ │
│  │  AC, Battery, Button, Thermal, Video, etc.           │ │
│  └──────────────────────────────────────────────────────┘ │
│                            │                               │
└────────────────────────────┼───────────────────────────────┘
                             │
                             ↓
┌────────────────────────────────────────────────────────────┐
│              Firmware (BIOS/EFI/UEFI)                      │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │  ACPI Tables │  │  AML Code    │  │  Power Logic │    │
│  │  (RSDP, DSDT│  │  (Methods,    │  │  (Reg. I/O,  │    │
│  │   SSDT, etc)│  │   AML code)   │  │   EC, etc.)  │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
│                                                             │
└────────────────────────────────────────────────────────────┘
                             │
                             ↓
┌────────────────────────────────────────────────────────────┐
│              Hardware                                      │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │   Devices    │  │  EC (Embed.  │  │  PCI Bridge  │    │
│  │  (CPU, Disk, │  │  Controller) │  │  (resource   │    │
│  │   etc.)      │  │              │  │   routing)   │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
│                                                             │
└────────────────────────────────────────────────────────────┘
```

---

## ARCHITECTURE & LAYERS

### Layer 1: Firmware Interface (ACPI Tables & AML)

#### ACPI Tables
```
RSDP (Root System Description Pointer)
  ↓ (fixed memory location 0xE0000-0xFFFFF)
  Points to RSDT/XSDT

RSDT/XSDT (Root System Description Table)
  ├─ FADT (Fixed ACPI Description Table)
  │   └─ Contains basic system parameters
  ├─ DSDT (Differentiated System Description Table)
  │   └─ Main AML code defining device tree
  ├─ SSDT (Secondary System Description Tables) [multiple]
  │   └─ Additional AML code, OEM-specific extensions
  ├─ MADT (Multiple APIC Description Table)
  │   └─ CPU and interrupt controller information
  ├─ MCFG (Memory-mapped Configuration Space)
  │   └─ PCI configuration space mappings
  ├─ SRAT (System Resource Affinity Table)
  │   └─ NUMA node information
  └─ [Other optional tables]

FADT Contents:
  - Interrupt model (APIC vs. PIC)
  - Platform revision level
  - Preferred Power Management Profile
  - Reset register location (memory/I/O)
  - Sleep type values (S1-S5 states)
  - Power button behavior
  - System management interrupt (SMI) handling
```

#### AML (ACPI Machine Language)
```
Bytecode Language: Similar to P-code/bytecode VM
Purpose: Define device tree, methods, and control logic

Structure:
  - DefinitionBlock: Container for AML definitions
    └─ Device: Device object definitions
       ├─ _HID: Hardware ID (vendor ID + device code)
       ├─ _UID: Unique ID (for multiple same devices)
       ├─ _STA: Status method (present? enabled? functional?)
       ├─ _CRS: Current Resource Settings
       ├─ _PRS: Possible Resource Settings
       ├─ _PRT: PCI Routing Table
       ├─ _PSxxx: Power state methods
       ├─ _Txx: Temperature sensor methods
       └─ Custom methods

Common Methods (underscore prefix):
  _HID(): Hardware ID (returns EISA ID like "PNP0A08" for PCI root)
  _UID(): Unique ID for device disambiguation
  _STA(): Status (bit 0=present, 1=enabled, 2=shown in UI, 3=functional)
  _INI(): Initialize device
  _CRS(): Current Resource Settings (memory, I/O, IRQ)
  _PRS(): Possible Resource Settings (supported configurations)
  _AEI(): ACPI Event Interrupt (GPIO events)
  _PSx(): Power State x (S0-S5 states)
  _Dxx(): Device State (D0-D3 power states)
  _PSW(): Power State Wake
  _PMD(): Power Management Device
  _ON():  Turn device on
  _OFF(): Turn device off
  _EJx(): Eject device
```

### Layer 2: ACPI CA - Component Architecture

#### Parser (drivers/acpi/parser/)
```
Task: Convert AML bytecode → Namespace objects

Process:
  1. Read DSDT/SSDT from memory
  2. Parse AML bytecode sequentially
  3. Create namespace objects for each definition
  4. Build device tree during parsing
  5. Store method code for later execution

Key Functions:
  - acpi_ps_parse_aml(): Main parsing entry
  - acpi_ps_parse_loop(): Parse individual opcodes
  - acpi_ps_get_next_package_length(): Get op size
  - acpi_ps_get_next_simple_arg(): Extract operands
```

#### Namespace (drivers/acpi/namespace/)
```
Task: Maintain the ACPI device tree hierarchy

Structure:
  Root
   ├─ _SB_  (System Bus)
   │  ├─ PCI0  (PCI Root Bridge)
   │  │  ├─ ISA  (LPC/ISA Bridge)
   │  │  │  ├─ EC0   (Embedded Controller)
   │  │  │  ├─ PS2   (Keyboard/Mouse)
   │  │  │  └─ COMA  (Serial Port)
   │  │  ├─ SLOT1, SLOT2  (PCI Slots)
   │  │  └─ ...
   │  ├─ CPU0, CPU1, ...  (Processors)
   │  └─ ...
   ├─ _GPE  (General Purpose Events)
   │  ├─ GPE0_STS, GPE0_EN  (Event blocks)
   │  └─ GPE1_STS, GPE1_EN
   ├─ _PR_  (Processor objects)
   │  ├─ CPU0, CPU1, ...
   │  └─ ...
   └─ _TZ_  (Thermal Zones)
      ├─ TZ0, TZ1, ...
      └─ ...

Operations:
  - acpi_ns_lookup(): Find object by path (e.g., "\\_SB.PCI0.ISA")
  - acpi_ns_walk_namespace(): Traverse tree
  - acpi_ns_attach_data(): Attach OS-specific data to node
  - acpi_get_name(): Get object path name
  - acpi_get_type(): Get object type
```

#### Dispatcher (drivers/acpi/dispatcher/)
```
Task: Execute AML methods when requested

Components:
  1. Method invocation routing
  2. Parameter passing and validation
  3. Return value handling
  4. Recursive method calls
  5. Control flow (if/else, switch, loops)

Key Functions:
  - acpi_ds_begin_method_execution(): Prepare method
  - acpi_ds_call_control_method(): Invoke method
  - acpi_ds_restart_control_method(): Resume after yield
  - acpi_ds_method_error(): Handle method errors

Flow:
  acpi_evaluate_object()     <- User requests method execution
    ↓
  acpi_ds_load_namespace()   <- Load and parse AML if needed
    ↓
  acpi_ds_begin_method_execution()  <- Prepare execution context
    ↓
  acpi_ex_resolve_to_value() <- Resolve operands
    ↓
  Execute opcode (from executer)
    ↓
  Return result to caller
```

#### Executer (drivers/acpi/executer/)
```
Task: Execute individual AML operations (VM for AML)

Operations Supported:
  - Arithmetic: Add, Subtract, Multiply, Divide, Modulo
  - Logical: And, Or, Xor, Not, Equal, Less, Greater
  - Bit Operations: Shift, BitField operations
  - String: Concatenate, Length, Substring
  - Object creation: CreateField, Package, Buffer
  - Control flow: If, Else, While, Break, Continue
  - Device access: Load, Unload, Notify
  - Scope traversal: TraverseScope, Navigate

Example Execution: Evaluating "_STA" method
  ┌─────────────────────────────┐
  │ _STA() {                    │  AML Code
  │   // Returns device status  │
  │   // Bit 0: Device present  │
  │   // Bit 1: Device enabled  │
  │   Return(0x0F)              │
  │ }                           │
  └─────────────────────────────┘
        ↓ (executer processes)
  ┌─────────────────────────────┐
  │ Push 0x0F onto stack        │  Execution
  │ Execute RETURN opcode       │  Steps
  │ Pop 0x0F, return to caller  │
  └─────────────────────────────┘
        ↓
  Return value: 0x0F (present + enabled + shown + functional)
```

#### Resources (drivers/acpi/resources/)
```
Task: Parse and manage hardware resources

Resources Tracked:
  - Memory ranges (fixed, cacheable, write-through, etc.)
  - I/O port ranges
  - Interrupt lines (IRQ)
  - DMA channels
  - PCI configuration space

Functions:
  - acpi_rs_get_crs_address(): Get current resources
  - acpi_rs_set_srs_address(): Set resources
  - acpi_rs_create_resource_list(): Parse _CRS
  - acpi_rs_get_prs_address(): Get possible resources
  - acpi_rs_get_prt_address(): Get PCI routing table

Example _CRS Method:
  Defines what resources device currently uses:
    Memory: 0x80000000 - 0x80000FFF (4KB)
    I/O: 0x2F8 - 0x2FF (8 bytes for serial port)
    IRQ: 3 (Interrupt request line 3)
```

#### Hardware Abstraction (drivers/acpi/hardware/)
```
Task: Low-level register I/O access

Registers Managed:
  - ACPI PM registers (power management)
  - General Purpose Event (GPE) blocks
  - Fixed event registers
  - Control/Status registers

Functions:
  - acpi_hw_read(): Read hardware register
  - acpi_hw_write(): Write hardware register
  - acpi_hw_enable_all_runtime_gpes(): Enable event handling
  - acpi_hw_set_mode(): Set ACPI mode vs. legacy mode
  - acpi_hw_get_mode(): Check current mode

Example: Reading power button event status
  1. Get PM register base address from FADT
  2. Calculate register offset
  3. Read 32-bit register
  4. Mask event bit
  5. Return status to event handler
```

### Layer 3: ACPI Bus Driver & Device Management

#### Bus Driver (drivers/acpi/bus.c)

```c
struct acpi_device {
    acpi_handle handle;              /* ACPI namespace handle */
    struct acpi_device *parent;      /* Parent device */
    struct list_head children;       /* Child devices */
    
    struct acpi_device_info info;    /* _HID, _UID, etc. */
    
    struct {
        u32 sta;                     /* _STA status */
    } status;
    
    struct {
        u32 state;                   /* Current power state D0-D3 */
    } power;
    
    struct {
        unsigned long eject_time;    /* For eject tracking */
    } removal;
    
    struct device dev;               /* Linux device model */
    struct acpi_driver *driver;      /* Bound driver */
    void *driver_data;               /* Driver private data */
    
    struct acpi_device_ops ops;      /* Device operations */
    struct acpi_device_flags flags;  /* Feature flags */
};
```

Key Operations:
- **Device Discovery**: Scan namespace, create device objects
- **Device Registration**: Register with Linux device model
- **Power Management**: Change device power states (_PSx methods)
- **Event Handling**: Respond to device events
- **Status Tracking**: Monitor device presence/functionality

#### Device Scanning (drivers/acpi/scan.c)

```
Scan Process:
  1. Start at ACPI root device
  2. For each namespace object:
     a. Get object type and info
     b. Check if it's a device (has _HID or _ADR)
     c. Create acpi_device structure
     d. Attach to Linux device model
     e. Recursively scan children
  3. Build device tree mirror in Linux

Example Scan Output:
  Scanning "\\_SB_"
    ├─ "SBAS" (System Bus)
    ├─ "PCI0" (PCI0 Root Bridge)
    │  ├─ "ISA" (ISA Bridge at PCI 0:31.0)
    │  │  ├─ "EC0" (Embedded Controller)
    │  │  └─ "PS2" (PS/2 Keyboard & Mouse)
    │  └─ [Other PCI devices]
    ├─ "CPU0", "CPU1" (Processors)
    └─ "TZ0" (Thermal Zone)
```

---

## CORE COMPONENTS

### 1. Event Processing System

#### Event Types
```c
/* Fixed Events */
#define ACPI_EVENT_PMTIMER          0
#define ACPI_EVENT_GLOBAL           1
#define ACPI_EVENT_POWER_BUTTON     2
#define ACPI_EVENT_SLEEP_BUTTON     3
#define ACPI_EVENT_RTC              4
#define ACPI_EVENT_CMOS_RTC         5
#define ACPI_EVENT_POWER_FAULT      6
#define NUM_FIXED_EVENTS            7

/* General Purpose Events (GPE) */
GPE_BLOCK_0: GPE bits 0-63   (EC related, etc.)
GPE_BLOCK_1: GPE bits 64-127 (Wake events, etc.)
```

#### Event Flow

```
Hardware Event (e.g., power button press)
    ↓ (asserts IRQ via SCI - System Control Interrupt)
    ↓
acpi_irq_handler() [in OSL - Operating System Layer]
    ↓
Read PM event status register
    ├─ Fixed event? (power button, sleep button, etc.)
    │  └─ Handle directly
    └─ GPE event? (General Purpose Event)
       └─ Queue for handling
    ↓
acpi_ev_gpe_handler()
    ↓
Execute GpeNotify method (if present)
    ↓
Call registered event handler (from driver)
    ├─ Power button handler
    ├─ Thermal event handler
    └─ Device-specific handler
    ↓
Notify user space (via /proc, netlink, input subsystem)
```

### 2. Power Management Subsystem

#### Power States

```
System Sleep States (S-states):
  S0 (Working)     : System fully powered, operational
  S1 (Standby)     : CPU stopped, memory powered, quick resume
  S2 (Suspend)     : More power savings than S1, longer resume
  S3 (Suspend to RAM): Most devices off, memory powered, slower resume
  S4 (Suspend to Disk): Minimal power, all state in disk, very slow resume
  S5 (Soft Off)    : Only minimal power for wake-up circuits
  
Device Power States (D-states):
  D0 (Working)     : Device fully powered, operational
  D1, D2 (Intermediate): Progressive power reduction
  D3hot (Warm idle): Device powered but not operational (can wake)
  D3cold (Cold idle): Device unpowered (cannot wake)

Power State Transitions:
  S0 (Working)
    ↓↑ (sleep/wake)
  S1, S2, S3 (Sleeping)
    ↓↑ (deep sleep/wake)
  S4 (Hibernation)
    ↓↑ (power off/on)
  S5 (Soft Off)
```

#### Device Power Operations

```c
int acpi_bus_set_power(acpi_handle handle, int state)
{
    /* Change device to specified power state */
    
    1. acpi_bus_get_device(handle, &device)
       └─ Get device structure
    
    2. device->power.state = state
       └─ Update power state variable
    
    3. acpi_evaluate_object(handle, "_PSx", ...)
       └─ Execute power state method
       └─ _PS0: Turn on device
       └─ _PS3: Turn off device
    
    4. Update device status flags
    
    5. Notify drivers if state changed
}

Example: Put device to sleep
  acpi_bus_set_power(handle, ACPI_STATE_D3)
    ├─ Call _PS3 method in ACPI firmware
    ├─ Method turns off power to device registers
    ├─ Device stops consuming power
    └─ Linux driver notified
```

### 3. Thermal Management

#### Temperature Monitoring

```
Thermal Zone (_TZ0, _TZ1, etc.):
  ├─ _TSP: Sampling period (milliseconds)
  ├─ _TC1: Thermal constant 1 (for threshold calculation)
  ├─ _TC2: Thermal constant 2
  ├─ _TRT: Thermal relationship table (device relationships)
  └─ Temperature reading methods:
     ├─ _TMP: Current temperature (in 1/10th Kelvin)
     └─ _Pxx: Passive thermal methods

Passive Cooling (no fans):
  1. Monitor temperature via _TMP
  2. If temperature rises above _PSV (passive trip point):
     └─ Reduce device power consumption
     └─ CPU frequency throttling
     └─ Reduce display brightness
  3. If temperature falls below _PTC (critical trip point):
     └─ Resume normal operation

Active Cooling (with fans):
  1. Monitor temperature
  2. If temperature rises:
     └─ Call _ON method to turn on cooling fan
     └─ Increase fan speed by increasing PWM duty cycle
  3. If temperature falls:
     └─ Call _OFF method to turn off fan
     └─ Decrease fan speed

Critical Temperature Handling:
  1. If temperature exceeds _CRT (critical trip point):
     └─ System immediately powers off (emergency shutdown)
     └─ To prevent hardware damage
```

#### Thermal Devices
```c
struct acpi_thermal {
    acpi_handle handle;
    struct acpi_device *device;
    
    acpi_thermal_state state;
    
    struct {
        unsigned long temperature;
        
        /* Temperature thresholds */
        unsigned long passive_temp;     /* _PSV */
        unsigned long critical_temp;    /* _CRT */
        unsigned long active_temp[10];  /* _AC0-_AC9 */
    } trips;
    
    struct acpi_thermal_active active[10];  /* Active cooling */
    struct acpi_thermal_passive passive;    /* Passive cooling */
    
    struct thermal_zone_device *thermal_zone;
};
```

---

## AML EXECUTION ENGINE

### Method Execution Flow

```
Example: Device power on via _ON method

ACPI Firmware AML Code (pseudocode):
  Device (WIFD)  // WiFi device
  {
    _HID = "PCI10EC"
    _UID = 0
    
    Method (_ON) {
      Store(1, \_SB.PCI0.MAWI)  // Set WiFi enable register
      Sleep(100)                 // Wait 100ms for device to power up
      Notify(WIFD, 0)            // Notify OS device is on
    }
    
    Method (_OFF) {
      Store(0, \_SB.PCI0.MAWI)  // Clear WiFi enable register
      Sleep(100)
      Notify(WIFD, 0)
    }
    
    Method (_STA) {
      If (\_SB.PCI0.MAWI == 1) {
        Return (0x0F)  // Present, enabled, shown, functional
      } Else {
        Return (0x00)  // Not present
      }
    }
  }

Linux Kernel Execution:
  1. Device power management requests: "Turn on WiFi"
     └─ acpi_bus_set_power(wifi_handle, D0)
  
  2. Power management resolves to method call
     └─ Look up _ON method via namespace
  
  3. Prepare execution context
     └─ Create method stack frame
     └─ Allocate local variables
  
  4. Parse and execute AML bytecode
     └─ Store opcode: Set memory location to value
     └─ Sleep opcode: Yield execution for 100ms
     └─ Notify opcode: Signal device state change
  
  5. Method returns
     └─ Cleanup stack frame
     └─ Return to Linux driver
  
  6. Driver acknowledges power state change
     └─ Device now operational at D0
```

### Namespace Method Resolution

```
Path Examples:
  \_SB.PCI0.ISA.EC0._CRS
    ↓
  Root \_SB (System Bus)
    ↓
  PCI0 (PCI Root Bridge)
    ↓
  ISA (ISA Bridge)
    ↓
  EC0 (Embedded Controller)
    ↓
  _CRS (Current Resource Settings method)

Lookup Process:
  1. Start at root or specified scope
  2. For each path component:
     a. Search namespace children
     b. Find exact match
     c. Move to that node
  3. Execute method if found
  4. Return result or error
```

---

## DEVICE INTEGRATION

### ACPI to Linux Device Model Mapping

```
ACPI Device Tree (from firmware)
  └─ acpi_device + acpi_driver
  
  ↓↓↓ (Linux Device Model Integration)
  
Linux struct device
  └─ Registered in sysfs
  └─ Driver binding
  └─ Power management hooks
  └─ Hotplug support
  
Example:

ACPI Namespace:
  \_SB.PCI0.ISA.EC0 (Embedded Controller)
    ├─ _HID = "PNP0C09"  (EC hardware ID)
    ├─ _CRS = (Memory: 0x80004000-0x80004000, IRQ: 9)
    └─ Methods: _INI, _REG, _QXX (event handlers)

Linux Kernel:
  1. scan.c: Find EC0 device, recognize _HID
  2. bus.c: Create acpi_device for EC0
  3. bus.c: Register with device model
  4. ec.c: Probe() - EC driver claims device
  5. sysfs: Appears as /sys/devices/LNXSYSTEM:00/LNXECAD:00/
  6. proc: Appears as /proc/acpi/embedded_controller/EC0/
```

### Device Binding Mechanism

```c
/* Device found during ACPI scan */
struct acpi_device *device = create_acpi_device(handle)

/* Search for matching driver */
struct acpi_driver *driver = find_driver_for_device(device)

if (driver && driver->ops.add) {
    /* Call driver's add function */
    result = driver->ops.add(device)
    
    if (result == 0) {
        device->driver = driver
        driver_data = device->driver_data
        
        /* Device now managed by driver */
        /* Driver handles:
           - Power management (suspend, resume)
           - Event handling (GPE callbacks)
           - Resource management
           - Device removal
        */
    }
}

/* If no driver found */
if (!device->driver) {
    /* Device unmanaged - ACPI handles directly */
    /* Fallback to platform driver or remain unused */
}
```

---

## POWER MANAGEMENT

### System Sleep Transitions

#### S3 Suspend to RAM Flow

```
User requests sleep:
  echo "mem" > /sys/power/state
    ↓
Linux PM subsystem:
  1. Suspend all running processes
  2. Suspend all devices:
     foreach device {
         driver->suspend(device)
         acpi_bus_set_power(device, D3)  /* Power off */
     }
  3. Freeze filesystems
  4. Disable interrupts
    ↓
ACPI Power Management:
  1. Enumerate all devices
  2. Execute _PTS method (Prepare to Sleep)
     └─ _PTS(3) for S3 state
     └─ Method prepares firmware for sleep
  3. Call acpi_hw_set_mode(ACPI_MODE_OFF)
     └─ Disable ACPI-controlled power
  4. Set sleep type register
  5. Set sleep enable register
    ↓
Hardware:
  1. Receives sleep enable signal
  2. Powers off most devices
  3. Keeps memory powered
  4. Keeps processors in sleep state
  5. Keeps only wake-up circuits powered
    ↓
Wake-up event (power button, etc.):
  1. Wake-up circuit detects event
  2. Powers on system
  3. Resumes CPU execution
    ↓
ACPI Resume:
  1. Early bootcode resumes BIOS
  2. BIOS checks wake reason
  3. Restores registers from backup
  4. Executes _WAK method (Wake)
     └─ _WAK(3) - acknowledge S3 wake
     └─ Re-enable system devices
  5. Returns control to kernel
    ↓
Linux Resume:
  1. Resume frozen filesystems
  2. Interrupt handling restored
  3. Resume each device:
     acpi_bus_set_power(device, D0)  /* Power on */
     driver->resume(device)
  4. Resume all processes
  5. Return to user space
    ↓
User program resumes:
  write() system call returns
  Program continues execution
```

### Device Power State Management

```
Device State Hierarchy:

Full Power (D0)
  ↓
Intermediate States (D1, D2) - optional
  ├─ Some logic still running
  ├─ May wake via PME (Power Management Event)
  ├─ Slower to return to D0 than from D1
  ↓
Light Sleep (D3hot)
  ├─ Device registers still powered
  ├─ Can wake system
  ├─ Fast transition to D0
  ↓
Deep Sleep (D3cold)
  ├─ All power removed from device
  ├─ Cannot wake system
  ├─ Slowest transition to D0
  └─ Used during system sleep or removal

Actual state management depends on:
  1. Device support (what states does firmware support?)
  2. Driver requirements (what states can driver handle?)
  3. Power policy (system sleep states)
  4. User requests (idle timeout, explicit user action)
```

---

## THERMAL MANAGEMENT

### Temperature Monitoring and Thermal Throttling

```
Active Thermal Management (with fans):
  
  Temperature Monitor (running continuously)
    ↓
  Read _TMP method (Current temperature in 1/10 K)
    ├─ Temperature < Active Trip (AC9): All fans OFF
    ├─ Active Trip (AC9) ≤ Temp < AC8: Fan at low speed
    ├─ AC8 ≤ Temp < AC7: Fan at medium speed
    └─ Temp ≥ AC0: Fan at maximum speed
    ↓
  If temperature approaches critical (_CRT):
    └─ Notify drivers to reduce workload
    └─ Prepare for emergency shutdown
    ↓
  If temperature exceeds critical (_CRT):
    └─ Immediate system shutdown (DID_NOT_CRASH, kernel shutdown)

Passive Thermal Management (no fans):
  
  CPU Throttling (P-states):
    High Temp → Reduce CPU frequency/voltage → Less heat
    Low Temp → Increase CPU frequency/voltage → More performance
  
  Device Power Reduction:
    High Temp → Turn off non-essential devices
    Low Temp → Resume normal operation
  
  Display Brightness:
    High Temp → Reduce brightness (saves power)
    Low Temp → Restore brightness
```

---

## EVENT HANDLING

### Fixed Events

```
Power Button Event (_PBT):
  1. Hardware power button pressed
  2. ACPI firmware signals event via interrupt
  3. Fixed event handler notified
  4. Execute power button notify method
  5. Notify init process (PID 1) for system shutdown
  
  Result: System begins graceful shutdown

Sleep Button Event:
  1. Hardware sleep button pressed (if available)
  2. Fixed event handler triggered
  3. Initiate system sleep (S3 or S4)
  4. Restore on wake

LID switch Event:
  1. Lid opened/closed
  2. Lid status read from EC (Embedded Controller)
  3. Device event queued
  4. Driver (video.c) notified
  5. Video device powered up/down accordingly
```

### General Purpose Events (GPE)

```
GPE Event Example: Battery status change

Firmware Setup:
  \_SB.PCI0.ISA.EC0._GPE  // General Purpose Event object
    ├─ _L17  // Method executed on GPE 0x17
    │  └─ Called when battery status changes
    │  └─ Notifies battery driver
    └─ _E20  // Method executed on GPE 0x20
       └─ Calls for device notification

When Battery Status Changes:
  1. EC hardware asserts GPE 0x17
  2. ACPI subsystem detects via PM event status register
  3. GPE handler retrieves GPE event number
  4. Looks up corresponding ACPI method (_L17)
  5. Executes method (from drivers/acpi/executer/)
  6. Method typically:
     - Reads battery status registers via EC
     - Updates driver-visible variables
     - Calls notify to wake battery driver
     - Signals sysfs attributes to update
  7. Battery driver (battery.c) wakes
  8. Re-reads battery status
  9. Updates user space via /proc/acpi/battery/
  10. User applications (acpid, power managers) react
```

---

## DATA FLOW & INITIALIZATION

### System Boot Sequence

```
1. BIOS/EFI Initialization
   ├─ Setup memory, CPU, basic hardware
   ├─ Build ACPI tables in memory
   ├─ Set up RSDP pointer at 0xE0000
   └─ Transfer control to bootloader

2. Linux Bootloader (GRUB, LILO)
   ├─ Load kernel image
   ├─ Pass boot parameters to kernel
   ├─ Set up initial page tables
   └─ Jump to kernel entry point

3. Linux Early Boot (arch-specific)
   ├─ arch/x86/kernel/setup.c: early_acpi_os_init()
   ├─ Reserve memory for ACPI tables
   ├─ Detect ACPI support
   └─ Mark ACPI tables as reserved (non-freeable)

4. ACPI Subsystem Initialization (drivers/acpi/bus.c)
   ├─ acpi_bus_init()
     ├─ Call acpi_os_initialize_tables()
     │  └─ Locate RSDP, RSDT/XSDT
     │  └─ Map ACPI tables into memory
     │  └─ Validate table checksums
     ├─ Call acpi_enable()
     │  └─ Set mode to ACPI mode (vs legacy PIC mode)
     │  └─ Initialize ACPI registers
     ├─ Call acpi_load_tables()
     │  └─ Parse DSDT (main AML)
     │  └─ Parse SSDT(s) (secondary AML)
     │  └─ Build namespace from AML
     └─ Call acpi_init_global_objects()
        └─ Initialize global variables for methods

5. ACPI Namespace Creation
   ├─ drivers/acpi/namespace/: Namespace initialization
   ├─ drivers/acpi/parser/: Parse AML bytecode
   │  └─ Build object tree
   ├─ Execute namespace _INI methods
   │  └─ Each device's initialization method
   └─ Namespace ready for device enumeration

6. ACPI Device Scanning (drivers/acpi/scan.c)
   ├─ acpi_scan_root_bridge()
     ├─ Start at root device (\\_SB)
     ├─ Recursively scan all children
     ├─ Create acpi_device for each device
     └─ Register with Linux device model
   ├─ For each PCI bridge found:
     └─ acpi_pci_root_add() (PCI integration)
        ├─ Enumerate PCI devices
        ├─ Create device tree
        ├─ Route PCI interrupts (via _PRT method)
        └─ Register PCI devices
   └─ Device tree now matches ACPI namespace

7. Device Driver Binding
   ├─ For each ACPI device:
     ├─ Look for matching driver
     │  └─ Match by _HID, _CID, or platform name
     ├─ Call driver->add()
     ├─ Initialize driver
     └─ Driver claims device
   └─ Multiple drivers may support same device
      (e.g., video could use intel_video or vga_switcheroo)

8. Final Setup
   ├─ Enable power button handling
   ├─ Enable GPE (General Purpose Event) handling
   ├─ Initialize event notifier handlers
   ├─ Setup thermal monitoring
   ├─ Register with sysfs
   ├─ Create /proc/acpi entries
   └─ ACPI subsystem ready for user I/O

System boot complete, ACPI operational
```

### Device Initialization Sequence

```
When a specific device (e.g., EC0 - Embedded Controller) is initialized:

1. ACPI Namespace Parsing
   ├─ Parser encounters EC0 device definition
   ├─ Recognizes _HID = "PNP0C09" (EC device)
   ├─ Recognizes _CRS = memory + IRQ resource
   ├─ Creates namespace node for EC0
   └─ Stores methods for later execution

2. EC Device Object Creation (scan.c)
   ├─ acpi_device created for EC0
   ├─ Device information populated:
   │  ├─ handle = pointer to namespace node
   │  ├─ parent = ISA device
   │  ├─ type = ACPI_TYPE_DEVICE
   │  ├─ hid = "PNP0C09"
   │  └─ status = result of _STA() evaluation
   └─ Device registered with Linux

3. Device Status Check
   ├─ Execute _STA method to check presence
   │  └─ If returns 0, device not present, skip
   │  └─ If returns non-zero, device present
   ├─ If status.present = 0:
   │  └─ Skip driver matching
   │  └─ Keep device object but dormant
   └─ If status.present = 1:
      └─ Proceed to driver matching

4. Driver Matching (glue.c)
   ├─ Search registered drivers for EC match
   ├─ EC driver (ec.c) recognizes _HID "PNP0C09"
   ├─ EC driver's probe function called
   └─ If successful:
      ├─ Set device->driver = ec_driver
      ├─ Call driver->add() to initialize

5. EC Driver Initialization (ec.c)
   ├─ Allocate acpi_ec structure
   ├─ Parse _CRS for memory/IRQ resources
   ├─ Request memory region
   ├─ Request IRQ
   ├─ Initialize EC state machine
   ├─ Execute _INI method (device-specific init)
   ├─ Setup _REG method (register OpRegion)
   │  └─ Allows firmware to access EC via AML
   ├─ Register GPE handlers
   │  └─ EC Query methods (_Qxx)
   └─ Device now operational

6. Device Available for Use
   ├─ /proc/acpi/embedded_controller/EC0/ visible
   ├─ Drivers can send commands to EC
   ├─ EC can notify kernel of events
   ├─ Other ACPI devices can use EC via AML
   └─ Battery, thermal, etc. can read status via EC
```

---

## KEY SUBSYSTEMS

### 1. Embedded Controller (EC) - drivers/acpi/ec.c

```
Purpose: Interface to embedded microcontroller
  - Manages battery status
  - Controls temperature sensors  
  - Handles lid switch
  - Manages keyboard backlight
  - Controls fan speed
  - Provides SMBus interface

Communication Protocol:
  Kernel → EC:
    1. Write command to EC command port (0x66)
    2. Write data bytes to EC data port (0x62)
    3. Poll EC status register for completion
  
  EC → Kernel:
    1. EC asserts SCI interrupt (System Control Interrupt)
    2. Kernel reads status register
    3. Kernel reads data from data port
    4. Query method (_Qxx) executed
    5. Handler processes result

EC Query Methods (_Qxx):
  _Q00, _Q01, ..., _QFF (256 possible)
  
  Each method handles specific EC event:
    _Q10: Battery status changed
    _Q20: Thermal threshold exceeded
    _Q30: Lid switch triggered
    _Q40: Keyboard hotkey pressed
    etc. (BIOS/firmware specific)
```

### 2. Battery Management - drivers/acpi/battery.c

```
Battery Information Retrieval:

Static Info (via _BIF method - Battery Information):
  - Battery name
  - Model number
  - Serial number
  - Battery technology (Li-ion, NiCd, etc.)
  - Design capacity (mAh)
  - Last full capacity (mAh)
  - Battery technology
  - Voltage (mV)
  - Alarm capacity (low battery threshold)
  - Manufacturer name
  - Manufacturer access number
  - Battery serial number
  - Battery type (Primary, Secondary)
  - OEM info

Dynamic Info (via _BST method - Battery Status):
  - Battery state (discharged, charging, discharging)
  - Present rate (current in mA)
  - Remaining capacity (mAh)
  - Present voltage (mV)

Remaining Time Calculation:
  If charging:
    time_to_full = (full_capacity - current_capacity) / charge_rate
  If discharging:
    time_to_empty = current_capacity / discharge_rate

User Interface:
  /proc/acpi/battery/BAT0/
    ├─ info          (static info)
    ├─ status        (current state, capacity, voltage)
    └─ alarm         (low battery threshold)
  
  sysfs:
    /sys/class/power_supply/BAT0/
```

### 3. AC Adapter Detection - drivers/acpi/ac.c

```
Purpose: Determine if system on AC or battery

Operation:
  1. Execute _PSR method (Power Source)
     └─ Returns 1 if on AC, 0 if on battery
  2. Monitor for status changes via GPE
  3. Notify battery manager of power source change

User Interface:
  /proc/acpi/ac_adapter/AC/
    └─ state
  
  sysfs:
    /sys/class/power_supply/AC/
```

### 4. Button Handling - drivers/acpi/button.c

```
Power Button:
  Triggers:
    - user holds power button
    - firmware generates fixed event
  
  Handler:
    - Notifies init process
    - System graceful shutdown
  
  BIOS Configuration:
    - Power button behavior configurable
    - Soft power (ACPI shutdown) vs. hard power off

Sleep Button:
  Triggers:
    - User presses sleep button (if available)
  
  Handler:
    - Triggers system sleep (S3 or S4)
    - Depends on user configuration

Lid Switch:
  Triggers:
    - Lid opened/closed
  
  Handler:
    - Managed by video driver
    - May power display on/off
    - May trigger sleep on close
```

### 5. Thermal Zone Management - drivers/acpi/thermal.c

```
Thermal Zone Methods:
  _TZD: Thermal zone devices (connected fans/heaters)
  _TSP: Polling interval (milliseconds)
  _PSV: Passive trip point (target temperature for passive cooling)
  _CRT: Critical temperature (emergency shutdown)
  _AC0-_AC9: Active cooling trip points (fan ON temperatures)
  _PSL: Passive cooling devices (CPUs, etc.)
  _AL0-_AL9: Active cooling device lists (fans)
  _TMP: Get temperature
  _DTI: Device Temperature Indication
  _HOT: Hot temperature (lower than critical)
  _TRT: Thermal Relationship Table
  _ART: Active Relationship Table

Temperature Trends:
  Passive cooling tracks temperature trend:
    If increasing: Reduce performance early
    If stable: Maintain current state
    If decreasing: Gradually increase performance

Hyperplane Method:
  temp_delta = current_temp - previous_temp
  trend = (temp_delta * _TC1) + (_TC2 * temp_change_rate)
  
  If trend > 0 and temp increasing:
    Reduce performance to prevent overheat
```

---

## DEVICE DRIVERS

### OEM-Specific Drivers

#### ASUS ACPI (asus_acpi.c)
```
Features:
  - Keyboard backlight control
  - LCD brightness
  - Fan speed control
  - Hotkey mapping
  - Wireless LED control
  - Storage expansion

Interface:
  /proc/acpi/asus/
    ├─ brightness
    ├─ lcd_switch
    ├─ hotkey
    └─ fan_speed
```

#### Toshiba ACPI (toshiba_acpi.c)
```
Features:
  - Backlight brightness
  - LCD power
  - Audio mute
  - Video output switching
  - Touchpad enable/disable
  - Cooling method selection

Interface:
  /proc/acpi/toshiba/
    ├─ lcd
    ├─ brightness
    ├─ audio
    └─ touchpad
```

#### IBM ACPI (ibm_acpi.c)
```
Features:
  - ThinkPad keyboard LED
  - Battery LED
  - Fan control
  - Thermal management
  - Hotkey handling
  - CMOS settings

Interface:
  /proc/acpi/ibm/
    ├─ cmos
    ├─ led
    ├─ fan
    ├─ thermal
    └─ hotkey
```

### Standard Device Drivers

#### Fan Control (fan.c)
```
Methods:
  _FIF: Fan Information
  _FPS: Fan Performance States
  _FSL: Fan Speed Level (write)
  _FST: Fan Status

States:
  0: Off
  1-N: Speed levels (OEM-dependent)

Passive mode:
  Kernel controls fan based on thermal zone
  
Active mode:
  BIOS controls fan, Linux monitors via _FPS
```

#### Video/Display (video.c)
```
Methods:
  _DOD: Enumerate output devices
  _DOS: Set output switching
  _ROM: Read ROM (BIOS code)
  _DCS: Check display connector status
  _DGS: Get graphics state
  _PSC: Get power state
  _ADA: Adaptive docking area?
  _DDC: Digital Data Channel

Outputs:
  LVDS (laptop panel)
  VGA (external monitor)
  HDMI (digital output)
  Composite (TV)

Switching:
  Kernel selects active outputs
  Can change between LCD/external
  Hotplug detection on connector change
```

---

## ADVANCED TOPICS

### Hotplug Device Support

```
Device Insertion:
  1. User inserts device (e.g., USB CardBus)
  2. Hardware detects insertion
  3. Firmware creates new ACPI namespace entry
  4. Firmware generates device check notification
  5. Kernel's device check handler triggered
  6. acpi_bus_scan() called for new device
  7. Device tree updated
  8. Drivers bound to new device
  9. Device operational

Device Removal:
  1. User ejects device
  2. Firmware may power down device
  3. Generates eject notification
  4. Kernel driver's remove() called
  5. Device resources released
  6. ACPI device object destroyed
  7. Removal complete, notification sent to firmware
```

### NUMA Support (numa.c)

```
Non-Uniform Memory Access:
  - Multi-socket systems with separate memory banks
  - Local memory: Fast access (same socket)
  - Remote memory: Slow access (different socket)

SRAT Table (System Resource Affinity Table):
  Describes NUMA topology:
  - CPU socket to memory bank mapping
  - Memory latency between nodes
  - Cache affinity

Kernel uses SRAT to:
  - Assign CPU to NUMA node
  - Assign memory pages to local node
  - Schedule threads on local CPU
```

### SBS/SMBus Support (sbs.c, i2c_ec.c)

```
Smart Battery System (SBS):
  Standard protocol for battery status
  
SMBus (System Management Bus):
  Low-speed bus for management
  Used for:
  - Battery info/status
  - Thermal sensors
  - Fan controllers
  - Power supplies

Access via EC:
  EC provides SMBus master interface
  Kernel can read/write SMBus devices via EC
  _REG method establishes access region
```

---

## CONFIGURATION & TUNING

### Kernel Configuration

```
CONFIG_ACPI
  Enable basic ACPI support

CONFIG_ACPI_SLEEP
  Enable system sleep states (S1-S4)

CONFIG_ACPI_AC
  Enable AC adapter detection

CONFIG_ACPI_BATTERY
  Enable battery monitoring

CONFIG_ACPI_BUTTON
  Enable power/sleep button handling

CONFIG_ACPI_FAN
  Enable fan control

CONFIG_ACPI_THERMAL
  Enable thermal zone management

CONFIG_ACPI_PROCESSOR
  Enable CPU power management (C-states, P-states)

CONFIG_ACPI_VIDEO
  Enable video/display switching

CONFIG_ACPI_DEBUG
  Enable debug output (verbose)

CONFIG_ACPI_BLACKLIST_YEAR
  BIOS blacklist before specified year (buggy ACPI)
```

### Common Issues & Workarounds

```
1. Broken BIOS ACPI:
   - Use "acpi=off" boot parameter
   - Falls back to legacy PM (APM, PIC)
   - Loses many features
   
2. Incorrect _STA evaluation:
   - Some BIOS report wrong device status
   - Device appears missing or broken
   - Use "acpi_force_table=file" to override DSDT
   
3. Battery not detected:
   - EC not initialized properly
   - Use "ec_intr=0" to use polling instead of interrupt
   - May indicate EC firmware bug
   
4. Fan always on:
   - Thermal constants incorrect
   - _TC1, _TC2 values wrong
   - Manual override via thermal.c parameters
   
5. Sleep not working:
   - _PTS method not implemented
   - _WAK method broken
   - Use acpi_sleep=s3_beep to debug

6. Wake not working:
   - Wake capabilities not set
   - GPE not configured for wake
   - Check _PRW method on devices
```

---

## PERFORMANCE CONSIDERATIONS

### Optimization Tips

```
1. Reduce Table Parsing Time:
   - Smaller DSDT tables parse faster
   - Trim unnecessary device definitions
   - Pre-compile AML to reduce interpretation

2. Cache Method Results:
   - _STA, _PSR often called repeatedly
   - Results vary slowly (if at all)
   - Driver can cache results

3. Minimize AML Execution:
   - Complex methods slow to execute
   - Use passive cooling instead of active (fewer method calls)
   - Batch device operations

4. Reduce Polling:
   - Use interrupts instead of polling (GPE)
   - Set reasonable thermal polling intervals (_TSP)
   - Avoid busy-waiting on EC responses

5. Memory Usage:
   - ACPI uses fixed memory for tables
   - Namespace uses memory for each device/method
   - Large systems with many devices use more memory
```

---

## DEBUGGING & TROUBLESHOOTING

### Debug Output

```
Boot with:
  acpi=verbose              - Verbose ACPI messages
  acpi_osi=Linux            - Set OS version to Linux
  acpi_osi="Windows 2001"   - Spoof Windows version
  
Kernel messages:
  dmesg | grep ACPI
  /var/log/kern.log
  
ACPI Debugger (if compiled):
  /sys/module/acpi/parameters/debug_level
  
  Values:
  1  = Init
  2  = Parse
  4  = Load namespace
  8  = Dispatch
  16 = Execute
  etc.
```

### Diagnostic Tools

```
acpidump: Dump ACPI tables (binary format)
acpicaexamine: Interactive ACPI namespace browser
iasl: ACPI source language compiler
       Can decompile DSDT to see AML
acpi_listen: Monitor ACPI events
acpidump -b: Extract BIOS tables to file
       Can be overridden with acpi_force_table boot param
```

---

## SUMMARY: Key Takeaways

1. **Firmware-Driven Design**: ACPI provides device descriptions and control methods in firmware (AML bytecode)
2. **Namespace-Based**: All devices organized in hierarchical namespace (tree)
3. **Method Execution**: AML methods executed by kernel VM (executer) on demand
4. **Event-Driven**: Hardware events trigger firmware methods, which notify kernel drivers
5. **Power Management**: Unified interface for system and device power states
6. **Thermal Control**: Automatic fan/throttling control based on temperature monitoring
7. **OS-Directed**: Operating system has control (vs. BIOS-directed like APM)
8. **Portability**: Same ACPI code on compatible hardware (vs. BIOS-specific code)

---

**Document Version**: 1.0
**Linux Kernel**: 2.6.20
**ACPI Specification**: Compliant with ACPI CA
**Last Updated**: 2024

