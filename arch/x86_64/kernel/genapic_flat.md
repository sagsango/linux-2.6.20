```text
===============================================================================
FLAT APIC AND PHYSICAL FLAT APIC MODES
File: arch/x86_64/kernel/genapic_flat.c
===============================================================================

PURPOSE
=======

This file implements APIC addressing policy for normal x86-64 SMP systems.

It provides two APIC modes:

    1. flat
    2. physical flat


These are not the low-level APIC register operations themselves.

Instead, this file tells generic APIC code:

    - which CPUs can receive IRQs
    - how to initialize logical APIC IDs
    - how to send IPIs
    - how to convert CPU masks into APIC IDs
    - whether to use logical or physical destination mode


===============================================================================
BACKGROUND: WHY APIC MODES EXIST
===============================================================================

The Local APIC can deliver interrupts using different addressing modes.

Main concepts:

    Logical destination mode
    Physical destination mode

Logical mode:

    Interrupt destination is a bitmask.

Physical mode:

    Interrupt destination is a physical APIC ID.


===============================================================================
FLAT MODE BIG PICTURE
===============================================================================

Flat mode is used for small SMP systems.

In flat mode:

    each CPU gets one logical APIC bit

Example:

    CPU0 -> bit 0 -> 00000001
    CPU1 -> bit 1 -> 00000010
    CPU2 -> bit 2 -> 00000100
    CPU3 -> bit 3 -> 00001000


Logical APIC mask:

    CPU0 + CPU2 = 00000101


Diagram:

              Logical APIC Destination Mask

                       00001101
                          |
          +---------------+----------------+
          |               |                |
          v               v                v

        CPU0            CPU2             CPU3


===============================================================================
LIMITATION OF FLAT MODE
===============================================================================

Flat logical APIC mode has limited bits.

Classic xAPIC flat logical mode effectively supports up to 8 CPUs.

Why?

    Logical destination field is 8 bits.

So:

    CPU0 -> bit 0
    CPU1 -> bit 1
    ...
    CPU7 -> bit 7

Beyond that, the bitmask overflows.

For larger systems, this file uses physical flat mode.


===============================================================================
PHYSICAL FLAT MODE BIG PICTURE
===============================================================================

Physical flat mode is used when logical mask would overflow.

Comment:

    Physflat mode is used when there are more than 8 CPUs on an AMD system.
    We cannot use logical delivery in this case because the mask overflows,
    so use physical mode.


Physical mode:

    destination = APIC ID of one CPU


Example:

    CPU10
      |
      v
    physical APIC ID = 0x0a


Instead of using a bitmask, the APIC message targets one physical APIC ID.


===============================================================================
GENERIC APIC STRUCTURE
===============================================================================

Both modes provide a struct genapic.

Generic APIC code calls function pointers from this structure.

Flat mode:

    struct genapic apic_flat

Physical flat mode:

    struct genapic apic_physflat


This means:

    apic.c = generic LAPIC code

    genapic_flat.c = addressing policy


===============================================================================
FLAT MODE STRUCTURE
===============================================================================

struct genapic apic_flat = {
    .name                     = "flat",
    .int_delivery_mode        = dest_LowestPrio,
    .int_dest_mode            = APIC_DEST_LOGICAL,
    .target_cpus              = flat_target_cpus,
    .vector_allocation_domain = flat_vector_allocation_domain,
    .apic_id_registered       = flat_apic_id_registered,
    .init_apic_ldr            = flat_init_apic_ldr,
    .send_IPI_all             = flat_send_IPI_all,
    .send_IPI_allbutself      = flat_send_IPI_allbutself,
    .send_IPI_mask            = flat_send_IPI_mask,
    .cpu_mask_to_apicid       = flat_cpu_mask_to_apicid,
    .phys_pkg_id              = phys_pkg_id,
};


Important:

    Delivery mode = Lowest Priority

Meaning:

    hardware may choose the lowest priority CPU among destination CPUs.


Destination mode:

    Logical


===============================================================================
PHYSICAL FLAT MODE STRUCTURE
===============================================================================

struct genapic apic_physflat = {
    .name                     = "physical flat",
    .int_delivery_mode        = dest_Fixed,
    .int_dest_mode            = APIC_DEST_PHYSICAL,
    .target_cpus              = physflat_target_cpus,
    .vector_allocation_domain = physflat_vector_allocation_domain,
    .apic_id_registered       = flat_apic_id_registered,
    .init_apic_ldr            = flat_init_apic_ldr,
    .send_IPI_all             = physflat_send_IPI_all,
    .send_IPI_allbutself      = physflat_send_IPI_allbutself,
    .send_IPI_mask            = physflat_send_IPI_mask,
    .cpu_mask_to_apicid       = physflat_cpu_mask_to_apicid,
    .phys_pkg_id              = phys_pkg_id,
};


Important:

    Delivery mode = Fixed

Meaning:

    interrupt goes to the chosen physical APIC ID.


Destination mode:

    Physical


===============================================================================
PART 1 : TARGET CPUS
===============================================================================

Flat:

    flat_target_cpus()
        return cpu_online_map


Physical flat:

    physflat_target_cpus()
        return cpu_online_map


Meaning:

    All online CPUs are valid interrupt targets.


Example:

    cpu_online_map = CPU0 CPU1 CPU2 CPU3

Then:

    target CPUs = CPU0 CPU1 CPU2 CPU3


===============================================================================
PART 2 : VECTOR ALLOCATION DOMAIN
===============================================================================

Flat:

    flat_vector_allocation_domain(int cpu)

Returns:

    APIC_ALL_CPUS


Why?

Because in flat mode with lowest priority delivery, hardware may deliver
to another CPU in the destination set.

Comment explains a hyperthreading issue:

    Some CPUs do not strictly honor the specified CPU destination when using
    lowest priority delivery mode.

So Linux allocates vectors as if all CPUs in the APIC mask may receive it.


Diagram:

Requested CPU:

    CPU2

But hardware might deliver to:

    CPU0 / CPU1 / CPU2 / CPU3

So vector allocation domain:

    all CPUs


Physical flat:

    physflat_vector_allocation_domain(int cpu)

Returns:

    only that CPU


Because physical fixed delivery targets exactly one APIC ID.


===============================================================================
PART 3 : INITIALIZE LOGICAL APIC ID
===============================================================================

Function:

    flat_init_apic_ldr()

Purpose:

    Configure APIC logical destination ID in flat mode.


Flow:

flat_init_apic_ldr()
      |
      +--> num = smp_processor_id()
      |
      +--> id = 1 << num
      |
      +--> x86_cpu_to_log_apicid[num] = id
      |
      +--> APIC_DFR = APIC_DFR_FLAT
      |
      +--> APIC_LDR = id


Example:

CPU0:

    id = 1 << 0 = 0x01

CPU1:

    id = 1 << 1 = 0x02

CPU2:

    id = 1 << 2 = 0x04


APIC_DFR:

    flat mode

APIC_LDR:

    logical ID bitmask


===============================================================================
APIC_DFR AND APIC_LDR
===============================================================================

APIC_DFR
--------

Destination Format Register.

Controls how logical destination is interpreted.

Flat mode:

    APIC_DFR_FLAT


APIC_LDR
--------

Logical Destination Register.

Contains this CPU's logical APIC ID.


Diagram:

CPU2 LAPIC:

    DFR = FLAT
    LDR = 00000100


===============================================================================
PART 4 : SEND IPI MASK IN FLAT MODE
===============================================================================

Function:

    flat_send_IPI_mask(cpumask, vector)

Flow:

flat_send_IPI_mask()
      |
      +--> mask = cpumask bits
      |
      +--> disable local interrupts
      |
      +--> wait APIC ICR idle
      |
      +--> write ICR2 with destination mask
      |
      +--> write ICR with vector + logical mode
      |
      +--> restore local interrupts


Important registers:

    APIC_ICR2
        destination field

    APIC_ICR
        command/vector field


Diagram:

CPU0 sends IPI to CPU1 + CPU3:

mask:

    00001010

APIC_ICR2:

    destination = 00001010

APIC_ICR:

    vector = RESCHEDULE_VECTOR
    destination mode = logical

Writing APIC_ICR sends the IPI.


===============================================================================
PART 5 : SEND IPI ALL BUT SELF
===============================================================================

Function:

    flat_send_IPI_allbutself(vector)

Two paths:

1. Hotplug CPU enabled OR NMI vector

    Build explicit mask:

        all online CPUs - current CPU

    Send using flat_send_IPI_mask()


2. Otherwise

    Use APIC shortcut:

        APIC_DEST_ALLBUT


Shortcut means hardware sends to all CPUs except self.


Why special case for NMI?

NMI delivery is safer with explicit CPU mask.


===============================================================================
PART 6 : SEND IPI ALL
===============================================================================

Function:

    flat_send_IPI_all(vector)

If vector is NMI:

    use explicit mask

Else:

    use APIC shortcut:

        APIC_DEST_ALLINC


Meaning:

    all including self


===============================================================================
PART 7 : APIC ID REGISTERED
===============================================================================

Function:

    flat_apic_id_registered()

Checks:

    GET_APIC_ID(apic_read(APIC_ID))

against:

    phys_cpu_present_map


Meaning:

    Is this CPU's physical APIC ID known as present?


===============================================================================
PART 8 : CPU MASK TO APIC ID IN FLAT MODE
===============================================================================

Function:

    flat_cpu_mask_to_apicid(cpumask)

Returns:

    cpumask low bits & APIC_ALL_CPUS


Because logical flat destination is a bitmask.


Example:

cpumask:

    CPU0 + CPU2

Mask:

    00000101

Return:

    00000101


===============================================================================
PART 9 : PHYSICAL PACKAGE ID
===============================================================================

Function:

    phys_pkg_id(index_msb)

Code:

    hard_smp_processor_id() >> index_msb


Reason:

    CPUID may return reset-time APIC ID.
    BIOS may change APIC IDs.

So use actual APIC ID register via hard_smp_processor_id().


===============================================================================
PHYSICAL FLAT MODE DETAILS
===============================================================================

Used when logical mode cannot represent all CPUs.

Example:

    more than 8 CPUs

Flat logical mask:

    only 8 bits

CPU10 cannot be represented as:

    1 << 10

inside 8-bit APIC logical destination.


So physical flat sends IPIs one CPU at a time using physical APIC IDs.


===============================================================================
PHYSFLAT SEND IPI MASK
===============================================================================

Function:

    physflat_send_IPI_mask(cpumask, vector)

Code:

    send_IPI_mask_sequence(cpumask, vector);


Meaning:

    Walk CPU mask and send to each physical APIC ID.


Flow:

mask = CPU1 CPU5 CPU9

send IPI to CPU1 physical APIC ID
send IPI to CPU5 physical APIC ID
send IPI to CPU9 physical APIC ID


===============================================================================
PHYSFLAT CPU MASK TO APIC ID
===============================================================================

Function:

    physflat_cpu_mask_to_apicid(cpumask)

Flow:

cpumask
   |
   +--> first_cpu(cpumask)
   |
   +--> x86_cpu_to_apicid[cpu]


Because fixed physical delivery can target only one APIC ID.

If no valid CPU:

    return BAD_APICID


===============================================================================
FLAT VS PHYSFLAT SUMMARY
===============================================================================

Flat Mode
---------

    Destination mode:
        logical

    Delivery mode:
        lowest priority

    CPU target:
        bitmask

    Best for:
        <= 8 CPUs

    IPI style:
        APIC logical mask / shortcut


Physflat Mode
-------------

    Destination mode:
        physical

    Delivery mode:
        fixed

    CPU target:
        physical APIC ID

    Best for:
        > 8 CPUs on AMD systems

    IPI style:
        send sequence to CPUs


===============================================================================
COMPLETE FLAT MODE FLOW
===============================================================================

Boot CPU / Secondary CPU setup
      |
      v
setup_local_APIC()
      |
      v
apic_flat.init_apic_ldr()
      |
      +--> logical ID = 1 << cpu
      |
      +--> APIC_DFR = FLAT
      |
      +--> APIC_LDR = logical ID
      |
      v
CPU ready


Runtime IPI
      |
      v
flat_send_IPI_mask()
      |
      +--> build logical destination mask
      |
      +--> write APIC_ICR2
      |
      +--> write APIC_ICR
      |
      v
Target CPUs receive vector


===============================================================================
COMPLETE PHYSFLAT FLOW
===============================================================================

Runtime IPI
      |
      v
physflat_send_IPI_mask()
      |
      +--> for each CPU in mask
      |
      +--> get physical APIC ID
      |
      +--> send fixed physical IPI
      |
      v
Target CPUs receive vector


===============================================================================
RELATION TO apic.c
===============================================================================

apic.c:

    setup_local_APIC()
    enable LAPIC
    setup timer
    handle APIC hardware basics


genapic_flat.c:

    decide APIC addressing policy


Generic code calls:

    genapic->send_IPI_mask()
    genapic->cpu_mask_to_apicid()
    genapic->init_apic_ldr()


Depending on selected mode:

    apic_flat
        or
    apic_physflat


===============================================================================
RELATION TO entry.S
===============================================================================

This file sends IPIs.

entry.S receives them.

Example:

CPU0 wants CPU1 to reschedule.

Sender side:

    flat_send_IPI_mask(CPU1, RESCHEDULE_VECTOR)

Receiver side:

    entry.S:
        reschedule_interrupt
            |
            v
        smp_reschedule_interrupt()


Flow:

CPU0
 |
 +--> APIC ICR write
 |
 v
CPU1 LAPIC
 |
 v
entry.S interrupt vector
 |
 v
C interrupt handler


===============================================================================
KEY IDEA
===============================================================================

This file answers the question:

    "How do we address CPUs through APIC on this machine?"


Flat mode:

    use logical APIC bitmasks

Physical flat mode:

    use physical APIC IDs


The rest of the kernel does not need to know those details.

It just calls generic APIC operations, and this file supplies the correct
implementation for the machine's APIC mode.
===============================================================================
```

