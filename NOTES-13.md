# qemu - pic, acpi, passthrough
================================================================================
                 KVM / QEMU / ACPI / PCI / IRQs — ONE GIANT ASCII FILE
================================================================================
Goal:
  Explain (from basics to in-depth) how:
    - PCI passthrough works
    - QEMU provides ACPI tables and PCI topology to the guest
    - Guest vs Host interrupts work (INTx vs MSI/MSI-X)
    - CPU address translation vs Device DMA translation
  All in ONE continuous ASCII reference.

Legend:
  GVA  = Guest Virtual Address
  GPA  = Guest Physical Address
  HPA  = Host Physical Address
  IOVA = I/O Virtual Address (device-side DMA address)
  INTx = Legacy pin-based interrupts (INTA#..INTD#)
  MSI  = Message Signaled Interrupt
  EPT/NPT = Second-level CPU page translation (Intel EPT / AMD NPT)
  IOMMU   = DMA remapping unit (Intel VT-d / AMD-Vi)
  vCPU = virtual CPU (runs guest code)
  VMM = Virtual Machine Monitor (QEMU)
  HV  = Hypervisor kernel component (KVM)

================================================================================
SECTION 1: WHAT IS RUNNING WHERE (THE BASIC SPLIT)
================================================================================

                     +--------------------------------------+
                     |              USERSPACE               |
                     |               (QEMU)                 |
                     |--------------------------------------|
                     | - emulated devices (PCI models)      |
                     | - virtio device backends             |
                     | - builds guest ACPI tables           |
                     | - presents PCI topology + config     |
                     | - main loop handles VM-exits         |
                     | - receives host IRQ events (VFIO)    |
                     +------------------+-------------------+
                                        |
                                        | ioctl(KVM_RUN), irqfd/eventfd, mmap(BAR)
                                        v
                     +--------------------------------------+
                     |               KERNEL                 |
                     |               (KVM)                  |
                     |--------------------------------------|
                     | - vCPU execution engine              |
                     | - VM-exit handling interface         |
                     | - EPT/NPT memory virtualization      |
                     | - interrupt injection into vCPU      |
                     | - hooks for assigned devices (VFIO)  |
                     +------------------+-------------------+
                                        |
                                        | hardware virtualization
                                        v
                     +--------------------------------------+
                     |                CPU                   |
                     |--------------------------------------|
                     | - runs guest code natively mostly    |
                     | - traps on privileged events/VM-exit |
                     | - delivers injected interrupts to    |
                     |   guest via virtual APIC/PIC model   |
                     +--------------------------------------+

Key mental model:
  - QEMU = platform + devices + ACPI tables + orchestration
  - KVM  = CPU + memory virtualization + interrupt injection plumbing

================================================================================
SECTION 2: THREE WAYS A GUEST "HAS A DEVICE"
================================================================================

(A) FULL EMULATION (classic QEMU device model)
----------------------------------------------

 Guest driver accesses device registers (PIO/MMIO)
     |
     |  (guest executes IN/OUT or MMIO load/store)
     v
 [VM-EXIT] due to I/O port or MMIO trap
     |
     v
 QEMU handles operation in userspace
     |
     v
 QEMU updates device model state
     |
     +--> if device wants to signal completion:
           QEMU requests KVM to inject interrupt into vCPU


(B) PARAVIRTUAL (VIRTIO)
------------------------

 Guest virtio driver uses shared memory rings (vring)
     |
     |  writes descriptors into guest memory (shared)
     v
 Host backend consumes descriptors (QEMU or vhost kernel)
     |
     v
 Completion posted
     |
     v
 Interrupt (usually MSI-X) injected into guest


(C) PCI PASSTHROUGH (DEVICE ASSIGNMENT)
---------------------------------------

 Guest sees a real PCI function
 Guest loads vendor driver
 Guest programs real BARs + MSI + DMA engines
 Hardware does real work
 Host mediates:
   - safe DMA via IOMMU
   - safe interrupt routing via VFIO->eventfd->KVM injection
 Guest sees interrupts and completion as if on bare metal

================================================================================
SECTION 3: ACPI TABLES IN QEMU (WHY THE GUEST BELIEVES THE PLATFORM EXISTS)
================================================================================

Real hardware story:
  ACPI describes platform + root bridges + interrupt topology
  PCI  discovers devices under those bridges

Same inside a VM:
  QEMU constructs ACPI tables to describe:
    - CPUs, LAPICs, IOAPICs (MADT)
    - power management interfaces (FADT/DSDT)
    - PCI config mechanism (MCFG for ECAM)
    - PCI root bridges and namespaces (DSDT/SSDT)
    - hotplug slots / memory hotplug (SSDT, methods)

Guest boot view:

  Guest firmware/bootloader/kernel
     |
     +--> Reads RSDP -> RSDT/XSDT -> DSDT/SSDT/MADT/FADT/MCFG
     |
     +--> Learns:
           - "There is a PCI root bridge here"
           - "Interrupt controller topology is like this"
           - "Use ECAM mapping for config space"
     |
     +--> Then guest OS performs PCI scanning and binds drivers

ASCII platform belief chain:

   QEMU (userspace)                        Guest OS
   -----------------                       ----------------------------
   Builds ACPI tables  ------------------>  Parses ACPI namespace
   Builds PCI topology ------------------>  Scans PCI buses/functions
   Provides IRQ topology ---------------->  Programs LAPIC/IOAPIC models
   Provides hotplug methods ------------->  Uses _STA/_CRS/_PSx, etc.

================================================================================
SECTION 4: PCI PASSTHROUGH — WHY IOMMU IS REQUIRED (DMA SAFETY)
================================================================================

The fundamental danger WITHOUT IOMMU:

  Guest controls device DMA addresses
      |
      v
  Device DMA writes to HOST physical memory directly
      |
      v
  Guest can overwrite:
     - host kernel memory
     - other VM memory
     - hypervisor structures
  => total escape / corruption

Thus: IOMMU is mandatory.

IOMMU provides second-stage translation for DMA:

                 (DMA address from device)
                       IOVA
                        |
                        v
                 +---------------+
                 |    IOMMU      |
                 |  (DMA paging) |
                 +---------------+
                        |
                        v
                       HPA
                 (real host RAM)

Host controls IOMMU page tables.
Guest cannot DMA outside allowed HPA ranges.

================================================================================
SECTION 5: TWO KINDS OF TRANSLATION (CPU vs DMA) — BOTH MATTER
================================================================================

(1) CPU ADDRESS TRANSLATION (for guest instructions)
----------------------------------------------------

Guest code uses virtual addresses (GVA):

          Guest instruction touches memory at GVA
                         |
                         v
            +----------------------------+
            | Guest page tables          |
            | (guest CR3, guest PTEs)    |
            +----------------------------+
                         |
                         v
                        GPA
                         |
                         v
            +----------------------------+
            | EPT/NPT (host controlled)  |
            | second-level translation   |
            +----------------------------+
                         |
                         v
                        HPA

So:

  GVA  --(guest PT)-->  GPA  --(EPT/NPT)-->  HPA


(2) DEVICE DMA TRANSLATION (for PCI device DMA engines)
-------------------------------------------------------

Device performs DMA using IOVA:

          Device DMA reads/writes at IOVA
                         |
                         v
            +----------------------------+
            | IOMMU page tables (host)   |
            | DMA remapping + perms      |
            +----------------------------+
                         |
                         v
                        HPA

So:

  IOVA --(IOMMU)--> HPA

Key combined fact:
  - Guest CPU memory uses EPT/NPT
  - Device DMA uses IOMMU
  Both must enforce isolation.

================================================================================
SECTION 6: GUEST vs HOST INTERRUPTS (THE CORE LAYERED REALITY)
================================================================================

On bare metal:

  Device ---> (IOAPIC/LAPIC) ---> CPU vector ---> ISR

Inside a VM, interrupts have layers:

  - Source may be:
      * emulated in QEMU
      * virtio backend
      * real passthrough hardware
  - Delivery into the guest CPU must be virtualized:
      * KVM injects into vCPU
      * guest sees it via virtual APIC/IOAPIC/PIC models

So every path ends with:
  "KVM inject interrupt into vCPU"

================================================================================
SECTION 7: INTERRUPT TYPES (INTx vs MSI/MSI-X) — INSIDE A VM
================================================================================

INTx (legacy pin-based):
------------------------
- Shared lines, level-triggered behavior is tricky
- Routed through IOAPIC/PIC
- Often requires emulation of deassert/EOI semantics carefully

MSI/MSI-X (message signaled):
-----------------------------
- Device writes a message (addr+data) to signal interrupt
- Cleaner routing
- Scales better
- Usually preferred for virtualization and passthrough

General virtualization preference:
  MSI/MSI-X is cleaner than INTx

================================================================================
SECTION 8: FULL END-TO-END PATHS (3 CASES)
================================================================================

===============================================================================
CASE 1: EMULATED PCI DEVICE (QEMU MODEL) — MMIO/PIO causes VM-EXIT
===============================================================================

          GUEST                                HOST
--------------------------------------------------------------------------------
  Guest driver does MMIO/PIO to device regs
     |
     v
  CPU executes MMIO load/store or IN/OUT
     |
     v
  [VM-EXIT]  (KVM_EXIT_MMIO or KVM_EXIT_IO)
     |
     v
  KVM returns to userspace (QEMU)
     |
     v
  QEMU device model handles read/write
     |
     v
  QEMU updates device state / schedules work
     |
     +-------------------------------+
     | completion happens            |
     v                               |
  QEMU raises virtual IRQ line / MSI |
     |                               |
     v                               |
  ioctl to KVM: inject interrupt ----+
     |
     v
  KVM queues interrupt for vCPU
     |
     v
  vCPU receives vector via virtual APIC/PIC
     |
     v
  Guest ISR runs

===============================================================================
CASE 2: VIRTIO (PARAVIRT) — shared rings reduce exits
===============================================================================

          GUEST                                HOST
--------------------------------------------------------------------------------
  Guest virtio driver writes descriptors into vring (guest memory)
     |
     v
  Guest "kicks" device (doorbell / notify)
     |
     v
  Host backend consumes vring (QEMU or vhost kernel thread)
     |
     v
  Host does I/O work (disk/network)
     |
     v
  Completion posted back to vring
     |
     v
  Host triggers interrupt (usually MSI-X)
     |
     v
  KVM injects interrupt into vCPU
     |
     v
  Guest ISR runs, consumes completions, continues

===============================================================================
CASE 3: PCI PASSTHROUGH (REAL DEVICE) — VFIO + IOMMU + KVM injection
===============================================================================

          GUEST                                    HOST
--------------------------------------------------------------------------------
  Guest OS enumerates device on PCI bus
     |
     v
  Guest loads real vendor driver
     |
     v
  Guest programs:
     - BARs (MMIO registers)
     - MSI/MSI-X vectors
     - DMA addresses (IOVA view)
     |
     v
  Real device performs real DMA + real interrupts
     |
     v
  (DMA path)                         (IRQ path)
  ---------                          ---------
  Device DMA IOVA                    Device MSI/INTx
     |                                   |
     v                                   v
  IOMMU remaps to HPA                Host interrupt arrives
     |                                   |
     v                                   v
  Host RAM                             VFIO owns device IRQ
                                         |
                                         v
                                    VFIO signals QEMU (eventfd)
                                         |
                                         v
                                    QEMU requests KVM injection
                                         |
                                         v
                                    KVM injects into vCPU
                                         |
                                         v
                                    Guest ISR runs

Important:
  Even though the device is real hardware, the GUEST CPU is virtual:
  host must still inject the interrupt into the vCPU execution context.

================================================================================
SECTION 9: MASTER "ALL-IN-ONE" DIAGRAM (PLATFORM + TABLES + PCI + DMA + IRQ)
================================================================================

                                 HOST MACHINE
================================================================================
   (A) Firmware / Host OS
   ----------------------
   Host has real PCI topology, real IOMMU, real interrupt controllers.

        +-------------------------------------------------------------+
        |                     REAL HOST HARDWARE                      |
        |-------------------------------------------------------------|
        |  Real PCI Device (GPU/NIC/etc)                              |
        |     |             |                                         |
        |     | DMA (IOVA)  | IRQ (MSI/MSI-X/INTx)                    |
        |     v             v                                         |
        |   IOMMU         Host IRQ controller (IOAPIC/LAPIC)           |
        +------|--------------------------|---------------------------+
               |                          |
               v                          v
        +-------------------+      +-------------------------------+
        |   IOMMU tables     |      |  Host kernel IRQ routing      |
        | (host controlled)  |      |  + VFIO interrupt handling    |
        +---------+----------+      +---------------+---------------+
                  |                               |
                  v                               v
                HPA RAM                    VFIO signals userspace
                                               (eventfd/irqfd)
                                                    |
                                                    v
================================================================================
   (B) KERNEL (KVM + VFIO)
   ----------------------

        +-------------------------------------------------------------+
        |                         HOST KERNEL                         |
        |-------------------------------------------------------------|
        |  KVM: vCPU execution + EPT/NPT + interrupt injection         |
        |  VFIO: safe device assignment + BAR exposure + DMA mapping   |
        +----------------------+----------------------+----------------+
                               |                      |
                               | KVM_RUN / inject IRQ | VFIO (mmap BAR,
                               v                      |  set up irqfd,
================================================================================  |  set up IOMMU maps)
   (C) USERSPACE (QEMU)                                                     |
   ----------------------                                                   |
                                                                            |
        +-------------------------------------------------------------+     |
        |                             QEMU                            |     |
        |-------------------------------------------------------------|     |
        |  - builds ACPI tables: DSDT/SSDT/MADT/FADT/MCFG...           |     |
        |  - presents PCI topology to guest                           |     |
        |  - emulates devices OR wires passthrough devices            |     |
        |  - main loop handles VM-exits                               |     |
        |  - receives VFIO IRQ events -> requests KVM injection        |<----+
        +----------------------+--------------------------------------+
                               |
                               | provides "platform reality"
                               v
================================================================================
                                 GUEST MACHINE
================================================================================
   (D) Guest View (ACPI + PCI + Drivers)
   ------------------------------------

  Guest boot:
    RSDP -> RSDT/XSDT -> DSDT/SSDT/MADT/FADT/MCFG
      |
      +--> learns: CPUs, LAPIC/IOAPIC, PCI roots, config mechanism
      |
      v
  Guest PCI scan under ACPI-described PCI root:
      |
      +--> discovers:
            - emulated devices (virtio/e1000/etc)
            - passthrough devices (real VID/DID/class)
      |
      v
  Guest loads drivers:
      - virtio drivers OR vendor driver (passthrough)
      |
      v
  Guest runtime:
      - memory accesses use GVA->GPA->HPA (EPT/NPT)
      - device DMA uses IOVA->HPA (IOMMU)
      - interrupts delivered via KVM injection into vCPU
================================================================================

================================================================================
SECTION 10: FINAL PINNED TAKEAWAYS
================================================================================

(1) QEMU provides the platform story:
    - ACPI tables (what guest believes exists)
    - PCI topology presentation (what guest enumerates)

(2) KVM provides CPU/memory virtualization:
    - vCPU execution (KVM_RUN)
    - EPT/NPT: GVA->GPA->HPA translation enforcement
    - interrupt injection into vCPU

(3) Passthrough requires IOMMU:
    - device DMA is contained by IOMMU: IOVA->HPA
    - without it, DMA breaks isolation

(4) Guest interrupts are always injected:
    - even real hardware interrupts must become a virtual interrupt to the vCPU

(5) MSI/MSI-X tends to virtualize cleaner than INTx:
    - fewer shared-line semantics
    - better scaling

================================================================================
END OF FILE
================================================================================

