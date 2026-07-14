
================================================================================
                    LINUX 2.6.20 PCI SUBSYSTEM CORE FLOW                        
================================================================================
 [BACKGROUND: The core PCI subsystem acts as a bridge between your physical ]
 [ peripheral chips and the Linux Device Driver Model. It handles hardware   ]
 [ discovery, maps system memory/IRQs, patches chipset bugs via quirks, and ]
 [ dynamically teardowns data structures during hotplug events.             ]
================================================================================

 [1. SUBSYSTEM INITIALIZATION] 
   │
   ├── pci.c ──────────────► Entry point via pci_init() initializes structures.
   ├── pci-driver.c ───────► Registers "pci_bus_type" with the kernel core.
   └── pci-sysfs.c/proc.c ─► Creates user-space nodes in /sys/bus/pci/ & /proc/.
   │
   ▼
 [2. BUS PROBING & TOPO] 
   │
   ├── access.c ───────────► Low-level config space read/write primitives.
   ├── probe.c ────────────► Scans Bus 0 -> Slot -> Function; creates pci_dev.
   └── search.c ───────────► Helper lookup routines to query existing buses.
   │
   ▼
 [3. QUIRK FIX-UPS (EARLY)]
   │
   └── quirks.c ───────────► Overrides & fixes broken hardware/non-spec behaviors.
   │
   ▼
 [4. RESOURCE ALLOCATION] 
   │
   ├── setup-bus.c ────────► Calculates memory/IO window sizes for bridges.
   ├── setup-res.c ────────► Assigns base address registers (BARs) to hardware.
   ├── setup-irq.c ────────► Connects legacy legacy wire lines (INTA-INTD).
   └── msi.c/msi.h ────────► Bypasses IO-APIC lines using Message Signaled IRQs.
   │
   ▼
 [5. EXTENDED BUS SERVICES]
   │
   ├── pcie/ ──────────────► Spawns PCIe port drivers (AER, Power Management).
   └── htirq.c ────────────► Sets up HyperTransport link-specific interrupts.
   │
   ▼
 [6. DRIVER MATCHING] 
   │
   └── pci-driver.c ───────► Runs pci_bus_match(); triggers driver .probe().
   │
   ▼
 [7. RUNTIME DYNAMICS] 
   │
   ├── hotplug.c ──────────► Abstract layer tracking live physical insertion.
   ├── hotplug/ ───────────► Vendor drivers (pciehp, acpiphp) catch physical pull.
   └── remove.c ───────────► Safe structural tear-down on device unplug events.

================================================================================
              GENERATED FOR KERNEL SOURCE CODE FLOW STUDY 
================================================================================

