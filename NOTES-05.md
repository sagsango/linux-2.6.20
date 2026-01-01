# 1. overview
================================================================================
                    X86 MODEL SPECIFIC REGISTERS (MSR)
                     COMPLETE FEATURE CLASSIFICATION
================================================================================

PURPOSE OF THIS FILE
--------------------
This file classifies ALL MSR-BASED FEATURES on x86 CPUs.
Every MSR that exists (documented or not) fits into ONE of these categories.

MSRs are NOT enumerable.
MSRs are NOT memory.
MSRs are the CPU CONTROL PLANE.

================================================================================
LEGEND
================================================================================
MSR   = Model Specific Register
CPU   = Logical CPU (core / SMT thread)
HV    = Hypervisor
VM    = Virtual Machine
CRx   = Control Registers (CR0, CR3, CR4, etc.)

================================================================================
HIGH-LEVEL MENTAL MODEL
================================================================================

    +-----------------------------+
    | CPUID                       |
    |  - What is possible         |
    +-------------+---------------+
                  |
                  v
    +-----------------------------+
    | MSRs                        |
    |  - What is enabled          |
    |  - How CPU behaves          |
    +-------------+---------------+
                  |
                  v
    +-----------------------------+
    | CRx / VMCS / VMCB           |
    |  - Active execution state   |
    +-----------------------------+

================================================================================
MSR FEATURE CATEGORIES (COMPLETE LIST)
================================================================================


================================================================================
1) CPU IDENTIFICATION & CAPABILITY CONTROL
================================================================================

PURPOSE
-------
Control high-level CPU features and irreversible configuration locks.

FEATURE TYPES
-------------
- Feature enable / disable bits
- One-time configuration locks
- Platform and SKU identification

USED FOR
--------
- Enabling VMX
- Locking CPU configuration
- Preventing late feature activation

CHARACTERISTICS
---------------
- Often write-once (locked)
- Incorrect values can permanently disable features until reboot

TYPICAL CONSUMERS
-----------------
- BIOS / Firmware
- Linux early boot
- Hypervisors


================================================================================
2) EXECUTION MODE & ISA CONTROL
================================================================================

PURPOSE
-------
Control CPU execution modes and ISA-level behavior.

FEATURE TYPES
-------------
- Long mode enable
- System call mechanisms
- Instruction behavior toggles

USED FOR
--------
- Entering 64-bit mode
- Enabling SYSCALL/SYSRET
- Enabling No-Execute (NX)

CHARACTERISTICS
---------------
- Often coupled with CR0 / CR4
- Required for mode transitions

TYPICAL CONSUMERS
-----------------
- Kernel boot code
- Context switch logic


================================================================================
3) MEMORY TYPE & CACHING CONTROL
================================================================================

PURPOSE
-------
Define how physical memory regions are cached.

FEATURE TYPES
-------------
- Cache policy tables
- Memory range attributes
- Cache flush controls

USED FOR
--------
- MMIO correctness
- DMA safety
- Framebuffer mappings

CHARACTERISTICS
---------------
- Affects system-wide performance
- Incorrect settings cause data corruption

TYPICAL CONSUMERS
-----------------
- Kernel memory manager
- Device drivers
- Firmware


================================================================================
4) PAGING & ADDRESS TRANSLATION SUPPORT
================================================================================

PURPOSE
-------
Enhance and control virtual-to-physical address translation.

FEATURE TYPES
-------------
- Paging mode enablement
- Page attribute extensions
- TLB behavior assistance

USED FOR
--------
- NX support
- Huge pages
- Advanced paging modes

CHARACTERISTICS
---------------
- Tightly coupled with MMU
- Used during boot and runtime

TYPICAL CONSUMERS
-----------------
- Kernel MM subsystem
- Hypervisors


================================================================================
5) VIRTUALIZATION (HYPERVISOR CONTROL)
================================================================================

PURPOSE
-------
Define and constrain hardware virtualization behavior.

FEATURE TYPES
-------------
- VMX / SVM enable bits
- Control-field capability masks
- Nested virtualization support

USED FOR
--------
- Validating VMCS/VMCB fields
- Enabling EPT / NPT
- VM entry/exit behavior

CHARACTERISTICS
---------------
- Heavily validated by hardware
- Misconfiguration causes VM entry failure

TYPICAL CONSUMERS
-----------------
- Hypervisors
- Firmware enabling virtualization


================================================================================
6) TIMEKEEPING & CLOCK CONTROL
================================================================================

PURPOSE
-------
Provide stable, precise time measurement.

FEATURE TYPES
-------------
- Timestamp counters
- Time scaling and adjustment
- Deadline-based timers

USED FOR
--------
- Scheduling
- Timekeeping
- Virtual machine clocks

CHARACTERISTICS
---------------
- Frequently virtualized
- Must remain monotonic

TYPICAL CONSUMERS
-----------------
- Kernel scheduler
- Hypervisors
- Time subsystems


================================================================================
7) PERFORMANCE MONITORING (PMU)
================================================================================

PURPOSE
-------
Observe and measure CPU behavior.

FEATURE TYPES
-------------
- Programmable counters
- Fixed-function counters
- Sampling controls

USED FOR
--------
- Profiling
- Performance tuning
- Debugging

CHARACTERISTICS
---------------
- High overhead if misused
- Highly CPU-generation specific

TYPICAL CONSUMERS
-----------------
- perf
- Profilers
- Kernel performance tools


================================================================================
8) POWER MANAGEMENT & FREQUENCY CONTROL
================================================================================

PURPOSE
-------
Control CPU power usage and frequency scaling.

FEATURE TYPES
-------------
- P-states
- C-states
- Turbo control

USED FOR
--------
- Power efficiency
- Thermal management
- Battery life

CHARACTERISTICS
---------------
- Strong hardware autonomy
- OS provides policy hints

TYPICAL CONSUMERS
-----------------
- cpufreq drivers
- Power management daemons


================================================================================
9) THERMAL MANAGEMENT
================================================================================

PURPOSE
-------
Prevent overheating and physical damage.

FEATURE TYPES
-------------
- Temperature sensors
- Throttling indicators
- Emergency shutdown triggers

USED FOR
--------
- Thermal throttling
- Hardware protection

CHARACTERISTICS
---------------
- Often read-only
- Hardware-enforced behavior

TYPICAL CONSUMERS
-----------------
- Kernel thermal framework
- Firmware
- Power management


================================================================================
10) SECURITY & SPECULATION CONTROL
================================================================================

PURPOSE
-------
Mitigate speculative execution and microarchitectural attacks.

FEATURE TYPES
-------------
- Speculation barriers
- Predictor control
- Cache flush commands

USED FOR
--------
- Spectre mitigations
- Meltdown mitigations
- Side-channel defense

CHARACTERISTICS
---------------
- Written on context switch
- Written on VM entry

TYPICAL CONSUMERS
-----------------
- Kernel
- Hypervisors


================================================================================
11) INTERRUPT & APIC CONTROL
================================================================================

PURPOSE
-------
Control interrupt routing and delivery.

FEATURE TYPES
-------------
- Local APIC enable
- APIC mode selection
- Timer configuration

USED FOR
--------
- Interrupt delivery
- CPU timers
- SMP coordination

CHARACTERISTICS
---------------
- Per-CPU state
- Performance critical

TYPICAL CONSUMERS
-----------------
- Kernel interrupt subsystem
- Hypervisors


================================================================================
12) DEBUGGING & EXECUTION TRACING
================================================================================

PURPOSE
-------
Provide low-level execution visibility.

FEATURE TYPES
-------------
- Branch tracing
- Execution history
- Debug stores

USED FOR
--------
- Debuggers
- Profilers
- Instruction tracing

CHARACTERISTICS
---------------
- High data volume
- Often disabled by default

TYPICAL CONSUMERS
-----------------
- perf
- ftrace
- Debug tools


================================================================================
13) MICROCODE & PATCH CONTROL
================================================================================

PURPOSE
-------
Patch CPU behavior after manufacturing.

FEATURE TYPES
-------------
- Microcode update triggers
- Patch revision reporting

USED FOR
--------
- Bug fixes
- Security updates

CHARACTERISTICS
---------------
- Highly privileged
- Vendor-controlled

TYPICAL CONSUMERS
-----------------
- Firmware
- Kernel microcode loader


================================================================================
14) SYSTEM FABRIC / UNCORE CONTROL
================================================================================

PURPOSE
-------
Control non-core CPU components.

FEATURE TYPES
-------------
- Interconnect control
- LLC behavior
- Memory controller tuning

USED FOR
--------
- Platform performance tuning
- Server-class optimizations

CHARACTERISTICS
---------------
- Often undocumented
- Platform-specific

TYPICAL CONSUMERS
-----------------
- Server firmware
- Vendor tools


================================================================================
15) MODEL-SPECIFIC / SILICON ERRATA CONTROLS
================================================================================

PURPOSE
-------
Work around CPU-generation-specific bugs.

FEATURE TYPES
-------------
- Errata workaround bits
- Stepping-specific controls

USED FOR
--------
- Hardware bug mitigation

CHARACTERISTICS
---------------
- Often undocumented
- CPU stepping dependent

TYPICAL CONSUMERS
-----------------
- Firmware
- Kernel CPU-specific code


================================================================================
16) VIRTUALIZATION-ONLY (SYNTHETIC / PARAVIRTUAL)
================================================================================

PURPOSE
-------
Fast guest-hypervisor communication.

FEATURE TYPES
-------------
- Paravirtual clocks
- Synthetic interrupts
- Hypercall interfaces

USED FOR
--------
- VM performance optimization

CHARACTERISTICS
---------------
- Exist only inside VMs
- Hypervisor-defined behavior

TYPICAL CONSUMERS
-----------------
- Guest kernels
- Hypervisor interfaces


================================================================================
17) TEST, VALIDATION & MANUFACTURING
================================================================================

PURPOSE
-------
Silicon validation and factory testing.

FEATURE TYPES
-------------
- Scan chains
- Test modes
- Manufacturing diagnostics

USED FOR
--------
- Factory testing
- Silicon validation

CHARACTERISTICS
---------------
- Undocumented
- Not for OS use

TYPICAL CONSUMERS
-----------------
- CPU manufacturers only


================================================================================
FINAL SUMMARY
================================================================================

ALL MSR FEATURES FALL INTO ONE OF THESE 17 CATEGORIES.

MSRs FORM:
----------
- The deepest CPU control plane
- The bridge between hardware and software policy
- The primary virtualization surface

CR REGISTERS  -> minimal architectural state
MSRs          -> everything else
CPUID         -> discovery only

================================================================================
END OF FILE
================================================================================









































# 2. details
================================================================================
                            X86 MODEL SPECIFIC REGISTERS (MSR)
================================================================================

LEGEND
------
MSR  = Model Specific Register
CPU  = Logical CPU (core / SMT thread)
VM   = Virtual Machine
HV   = Hypervisor
CRx  = Control Registers (CR0, CR3, CR4, ...)
--------------------------------------------------------------------------------


================================================================================
1) WHAT AN MSR IS (CORE IDEA)
================================================================================

    +------------------------------------------------------------+
    | MSR = CPU-INTERNAL REGISTER ACCESSED BY INDEX NUMBER       |
    +------------------------------------------------------------+

    - Not RAM
    - Not MMIO
    - Not visible on PCI / memory bus
    - Exists only inside the CPU core
    - Accessed ONLY via RDMSR / WRMSR instructions
    - Privileged (Ring 0) unless virtualized


--------------------------------------------------------------------------------
ACCESS MECHANISM
--------------------------------------------------------------------------------

        ECX = MSR_INDEX
        RDMSR
        -------------------->
        EDX:EAX = 64-bit MSR VALUE


        EDX:EAX = VALUE
        ECX     = MSR_INDEX
        WRMSR
        -------------------->
        CPU STATE CHANGES


INVALID MSR ACCESS  --->  #GP (General Protection Fault)


================================================================================
2) WHERE MSRS LIVE (PHYSICALLY)
================================================================================

    +------------------------------------------------------------+
    | MSRs LIVE INSIDE THE CPU CORE ITSELF                       |
    +------------------------------------------------------------+

    IMPLEMENTATION (conceptual):
    ----------------------------
    - Flip-flops
    - Latches
    - Microcode-backed state
    - Per-logical-CPU storage

    NOT STORED IN:
    --------------
    - RAM
    - ROM
    - BIOS flash
    - MMIO
    - Disk

    RESET BEHAVIOR:
    ---------------
    - Power cycle      -> reset
    - CPU reset        -> reset
    - INIT / SIPI      -> reset
    - VM-entry         -> loaded from VMCS/VMCB
    - VM-exit          -> restored to host values


================================================================================
3) MSR VS OTHER CPU MECHANISMS
================================================================================

    +----------------+----------------+----------------+----------------+
    | MECHANISM      | ACCESS         | PURPOSE        | STORAGE        |
    +----------------+----------------+----------------+----------------+
    | CPUID          | Unprivileged   | Capability     | Generated      |
    | CR0-CR8        | Privileged     | Core control   | CPU regs       |
    | MSR            | Privileged     | Deep control   | CPU internal   |
    | VMCS / VMCB    | Privileged     | VM snapshot    | RAM structure  |
    +----------------+----------------+----------------+----------------+

    CPUID  -> "What is possible?"
    MSR    -> "What is actually enabled?"
    CRx    -> "Architectural control"
    VMCS   -> "Virtual CPU image"


================================================================================
4) MSR CATEGORIES (BIG PICTURE)
================================================================================

    MSRs ARE GROUPED BY FUNCTION:

        A) CPU IDENTIFICATION & FEATURE CONTROL
        B) VIRTUALIZATION (VMX / SVM)
        C) MEMORY MANAGEMENT & PAGING
        D) TIME & PERFORMANCE
        E) POWER & THERMAL
        F) SECURITY & MITIGATIONS
        G) DEBUGGING & TRACING


================================================================================
5) A) CPU IDENTIFICATION & FEATURE CONTROL
================================================================================

    PURPOSE:
    --------
    - Enable / disable CPU features
    - Lock configuration
    - Prevent late changes

    IMPORTANT MSRS:
    ---------------

    MSR: IA32_FEATURE_CONTROL (0x0000003A)

        63.....................................................0
        +-------------------------------------------------------+
        |                   RESERVED                           |
        +------------------------+----------------+-------------+
                                 | VMX inside SMX|
                                 | VMX outside   |
                                 | SMX           |
                                 | LOCK          |
                                 +-----------------------------+

        BIT 0  : LOCK (once set, cannot change until reboot)
        BIT 1  : VMX allowed in SMX
        BIT 2  : VMX allowed outside SMX

        WRONG CONFIGURATION = VMX PERMANENTLY DISABLED


    MSR: IA32_MISC_ENABLE (0x000001A0)
        - Fast strings
        - Thermal control
        - Turbo enable/disable
        - Performance features


================================================================================
6) B) VIRTUALIZATION MSRS
================================================================================

-----------------------
INTEL VMX MSRS
-----------------------

    THESE DEFINE WHAT VMCS BITS ARE LEGAL

    MSR: IA32_VMX_BASIC
        - VMCS revision ID
        - VMCS memory type
        - VMCS size

    MSR: IA32_VMX_PINBASED_CTLS
    MSR: IA32_VMX_PROCBASED_CTLS
    MSR: IA32_VMX_PROCBASED_CTLS2
    MSR: IA32_VMX_EXIT_CTLS
    MSR: IA32_VMX_ENTRY_CTLS

        EACH CONTROL MSR:
        -----------------
        LOW  32 bits -> allowed 0s
        HIGH 32 bits -> allowed 1s

        (bit must be 0 if low=0, must be 1 if high=1)


    MSR: IA32_VMX_EPT_VPID_CAP
        - EPT support
        - Large pages
        - INVEPT / INVVPID support


-----------------------
AMD SVM MSRS
-----------------------

    MSR: VM_CR
        - SVM enable bit

    MSR: VM_HSAVE_PA
        - Physical address of host save area

    MSR: SVM_FEATURES
        - Nested paging
        - Decode assists
        - AVIC


================================================================================
7) C) MEMORY MANAGEMENT & PAGING
================================================================================

    MSR: IA32_EFER (0xC0000080)

        63.....................................................0
        +-------------------------------------------------------+
        |        RESERVED        |NXE|LMA|LME|SCE|
        +-------------------------------------------------------+

        BIT 0  : SCE  -> SYSCALL/SYSRET enable
        BIT 8  : LME  -> Long mode enable
        BIT 10 : LMA  -> Long mode active (read-only)
        BIT 11 : NXE  -> No-Execute enable

        BOOT FLOW:
        ----------
        - Enable PAE
        - Set EFER.LME
        - Load CR3
        - Enable paging
        - CPU enters long mode


    MSR: IA32_PAT
        - Defines cache types per PAT index

    MSR: IA32_MTRR_* family
        - Variable MTRRs
        - Fixed MTRRs
        - Physical memory type control


================================================================================
8) D) TIME & PERFORMANCE
================================================================================

    MSR: IA32_TSC
        - Timestamp counter

    MSR: IA32_TSC_ADJUST
        - VM time correction
        - Used by hypervisors

    MSR: IA32_PERF_GLOBAL_CTRL
        - Enable PMU counters

    MSR: IA32_FIXED_CTR0..2
        - Instructions retired
        - Cycles
        - Reference cycles

    MSR: IA32_PMC0..N
        - Programmable counters


================================================================================
9) E) POWER & THERMAL
================================================================================

    MSR: IA32_APERF / IA32_MPERF
        - Actual vs maximum frequency

    MSR: IA32_THERM_STATUS
        - Core temperature
        - Throttling flags

    MSR: IA32_PKG_POWER_LIMIT
        - Package power caps

    MSR: IA32_POWER_CTL
        - C-states
        - Energy features


================================================================================
10) F) SECURITY & MITIGATIONS
================================================================================

    MSR: IA32_SPEC_CTRL
        - IBRS
        - STIBP
        - SSBD

    MSR: IA32_PRED_CMD
        - Branch predictor flush

    MSR: IA32_FLUSH_CMD
        - L1D cache flush

    MSR: IA32_ARCH_CAPABILITIES
        - CPU vulnerability flags

    KERNEL USE:
    -----------
    - Written on context switch
    - Written on VM-entry
    - Written on syscall entry


================================================================================
11) G) DEBUGGING & TRACING
================================================================================

    MSR: IA32_DEBUGCTL
        - LBR enable
        - BTS enable

    MSR: IA32_DS_AREA
        - Debug store area pointer

    MSR: IA32_LBR_* family
        - Last Branch Records

    USED BY:
    --------
    - perf
    - ftrace
    - Intel PT
    - kernel debuggers


================================================================================
12) MSRS IN VIRTUALIZATION (CRITICAL)
================================================================================

    FOR EACH MSR, HYPERVISOR CHOOSES:

        +------------------+----------------------------------+
        | STRATEGY         | EXAMPLE                          |
        +------------------+----------------------------------+
        | PASS THROUGH     | IA32_EFER                        |
        | TRAP & EMULATE   | IA32_TSC                         |
        | FAKE / HIDE      | IA32_FEATURE_CONTROL             |
        +------------------+----------------------------------+

    KVM STRUCTURE (CONCEPTUAL):

        vcpu
         |
         +-- arch
              |
              +-- guest_msrs[]
              +-- host_msrs[]


    VM-ENTRY:
    ---------
    - Load guest MSRs

    VM-EXIT:
    --------
    - Restore host MSRs


================================================================================
13) HOW MANY MSRS EXIST?
================================================================================

    - NO FIXED NUMBER
    - HUNDREDS PER VENDOR
    - MANY RESERVED
    - MANY UNDOCUMENTED
    - INVALID ACCESS => #GP


================================================================================
14) LINUX KERNEL BOOT MSR FLOW (SIMPLIFIED)
================================================================================

    start_kernel
        |
        +-- cpu_init
        |     |
        |     +-- rdmsr(EFER)
        |     +-- wrmsr(EFER | NX | SCE)
        |
        +-- setup_pat
        |
        +-- setup_mtrr
        |
        +-- setup_pmu
        |
        +-- setup_security_msrs
        |
        +-- enable_userspace


================================================================================
15) FINAL MENTAL MODEL
================================================================================

    CRx REGISTERS
        |
        v
    ARCHITECTURAL CONTROL (small, fixed)

    MSRS
        |
        v
    DEEP CPU BEHAVIOR CONTROL

    CPUID
        |
        v
    CAPABILITY DISCOVERY

    VMCS / VMCB
        |
        v
    VIRTUAL CPU SNAPSHOT


================================================================================
END OF FILE
================================================================================

