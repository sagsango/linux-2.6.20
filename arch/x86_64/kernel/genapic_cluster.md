```text
===============================================================================
CLUSTERED APIC MODE
File: arch/x86_64/kernel/genapic_cluster.c
===============================================================================

PURPOSE
=======

This file implements the "clustered" APIC mode for large x86-64 SMP systems.

It is used on systems where CPUs are grouped into APIC clusters.

Goal:

    Support large CPU count systems using APIC interrupt delivery.

Comment says:

    Up to 255 CPUs, physical delivery.
    Realistic maximum around 230 CPUs.


===============================================================================
BACKGROUND: NORMAL APIC VS CLUSTERED APIC
===============================================================================

Simple APIC system:

    CPU0 APIC ID = 0
    CPU1 APIC ID = 1
    CPU2 APIC ID = 2
    CPU3 APIC ID = 3


Large clustered system:

    CPUs are grouped into clusters.

Example:

    Cluster 0:
        CPU0
        CPU1
        CPU2
        CPU3

    Cluster 1:
        CPU4
        CPU5
        CPU6
        CPU7

    Cluster 2:
        CPU8
        CPU9
        CPU10
        CPU11


APIC ID contains:

    cluster number
    local CPU bit inside cluster


===============================================================================
WHY CLUSTERED MODE EXISTS
===============================================================================

Old xAPIC logical destination mode has limitations.

In cluster mode:

    Logical APIC ID layout roughly looks like:

        [ cluster id ][ local id bitmap ]

The local bitmap has only 4 bits.

That means only 4 unique logical destination bits per cluster.

But this implementation uses physical delivery for IRQs, so it does not
depend heavily on unique logical bitmap bits for every CPU.


===============================================================================
HIGH LEVEL ARCHITECTURE
===============================================================================

                    Interrupt / IPI
                          |
                          v

                +-------------------+
                | APIC routing code |
                +---------+---------+
                          |
                          v

       +------------------+------------------+
       |                                     |
       v                                     v

+-------------------+               +-------------------+
| Cluster 0         |               | Cluster 1         |
|                   |               |                   |
| CPU0 LAPIC        |               | CPU4 LAPIC        |
| CPU1 LAPIC        |               | CPU5 LAPIC        |
| CPU2 LAPIC        |               | CPU6 LAPIC        |
| CPU3 LAPIC        |               | CPU7 LAPIC        |
+-------------------+               +-------------------+


===============================================================================
IMPORTANT DATA STRUCTURE
===============================================================================

struct genapic apic_cluster = {
    .name                     = "clustered",
    .int_delivery_mode        = dest_Fixed,
    .int_dest_mode            = APIC_DEST_PHYSICAL,
    .target_cpus              = cluster_target_cpus,
    .vector_allocation_domain = cluster_vector_allocation_domain,
    .apic_id_registered       = cluster_apic_id_registered,
    .init_apic_ldr            = cluster_init_apic_ldr,
    .send_IPI_all             = cluster_send_IPI_all,
    .send_IPI_allbutself      = cluster_send_IPI_allbutself,
    .send_IPI_mask            = cluster_send_IPI_mask,
    .cpu_mask_to_apicid       = cluster_cpu_mask_to_apicid,
    .phys_pkg_id              = phys_pkg_id,
};

Meaning:

    This file provides APIC operations for clustered machines.

Generic APIC code calls function pointers in this structure.


===============================================================================
PART 1 : INITIALIZE LOGICAL APIC ID
===============================================================================

Function:

    cluster_init_apic_ldr()

Purpose:

    Setup APIC logical destination register.

Registers:

    APIC_DFR = Destination Format Register
    APIC_LDR = Logical Destination Register


Flow:

cluster_init_apic_ldr()
      |
      +--> read current physical APIC ID
      |
      +--> extract cluster ID
      |
      +--> count CPUs already in same cluster
      |
      +--> choose logical bit
      |
      +--> program DFR as cluster mode
      |
      +--> program LDR with logical ID


===============================================================================
APIC ID AND CLUSTER ID
===============================================================================

Code:

    my_id = hard_smp_processor_id();
    my_cluster = APIC_CLUSTER(my_id);


Meaning:

    Read real hardware APIC ID.

    Extract cluster portion from APIC ID.


Example:

    APIC ID = 0x21

Could mean:

    cluster = 0x20
    local   = 0x01


===============================================================================
COUNT CPUS ALREADY IN CLUSTER
===============================================================================

Code idea:

    for each CPU:
        lid = x86_cpu_to_log_apicid[i]
        if lid belongs to my cluster:
            count++


Purpose:

    Choose next logical bit inside this cluster.


Example:

Before CPU joins:

    Cluster 0 already has:
        CPU0 -> bit 0
        CPU1 -> bit 1

New CPU gets:

        bit 2


===============================================================================
ONLY 4 LOGICAL BITS PER CLUSTER
===============================================================================

Comment:

    We only have a 4 wide bitmap in cluster mode.

So:

    bit 0
    bit 1
    bit 2
    bit 3

If more than 4 CPUs in same cluster:

    use bit 3 for 4th through Nth CPU


Why acceptable?

Because this code uses physical IRQ delivery.

So unique logical ID is less critical.


===============================================================================
PROGRAM DFR AND LDR
===============================================================================

Code:

    apic_write(APIC_DFR, APIC_DFR_CLUSTER);

    val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
    val |= SET_APIC_LOGICAL_ID(id);
    apic_write(APIC_LDR, val);


Meaning:

    Put LAPIC into clustered logical mode.

    Set this CPU's logical APIC ID.


Diagram:

CPU LAPIC
   |
   +--> DFR = cluster mode
   |
   +--> LDR = cluster_id | local_bit


===============================================================================
PART 2 : TARGET CPUS FOR IRQs
===============================================================================

Function:

    cluster_target_cpus()

Returns:

    cpumask_of_cpu(0)


Meaning:

    Initially send all IRQs to boot CPU.

Comment:

    Start with all IRQs pointing to boot CPU.
    IRQ balancing will shift them.


Flow:

Early IRQ routing
      |
      v
CPU0 handles IRQs first
      |
      v
IRQ balancer may move IRQs later


===============================================================================
PART 3 : VECTOR ALLOCATION DOMAIN
===============================================================================

Function:

    cluster_vector_allocation_domain(int cpu)

Returns:

    mask containing only that CPU.


Meaning:

    Allocate interrupt vector per CPU.

Flow:

Request vector for CPU N
      |
      v
Domain = { CPU N }


===============================================================================
PART 4 : SEND IPI TO MASK
===============================================================================

Function:

    cluster_send_IPI_mask(mask, vector)

Code:

    send_IPI_mask_sequence(mask, vector);


Meaning:

    Send the IPI to CPUs in the mask one by one.


Example:

    mask = CPU1, CPU3, CPU5
    vector = RESCHEDULE_VECTOR

Flow:

send IPI to CPU1
send IPI to CPU3
send IPI to CPU5


===============================================================================
PART 5 : SEND IPI TO ALL BUT SELF
===============================================================================

Function:

    cluster_send_IPI_allbutself(vector)

Flow:

mask = cpu_online_map
remove current CPU
if mask not empty:
    send IPI to mask


Example:

Current CPU = CPU2

Online CPUs:

    CPU0 CPU1 CPU2 CPU3

After removing self:

    CPU0 CPU1 CPU3

Send IPI to:

    CPU0 CPU1 CPU3


===============================================================================
PART 6 : SEND IPI TO ALL
===============================================================================

Function:

    cluster_send_IPI_all(vector)

Flow:

send IPI to cpu_online_map


Example:

Online CPUs:

    CPU0 CPU1 CPU2 CPU3

Send vector to all of them.


===============================================================================
PART 7 : APIC ID REGISTERED
===============================================================================

Function:

    cluster_apic_id_registered()

Returns:

    1


Meaning:

    Always assume APIC ID is registered/valid in this mode.


===============================================================================
PART 8 : CPU MASK TO APIC ID
===============================================================================

Function:

    cluster_cpu_mask_to_apicid(cpumask)

Purpose:

    Convert Linux CPU mask to physical APIC ID.


Important comment:

    We're using fixed IRQ delivery,
    can only return one physical APIC ID.
    May as well be the first.


Flow:

cpumask
   |
   +--> find first CPU
   |
   +--> return x86_cpu_to_apicid[cpu]


Example:

cpumask = { CPU2, CPU5, CPU7 }

first_cpu = CPU2

return:

    APIC ID of CPU2


===============================================================================
PART 9 : PHYSICAL PACKAGE ID
===============================================================================

Function:

    phys_pkg_id(index_msb)

Code:

    return hard_smp_processor_id() >> index_msb;


Why not CPUID?

Comment says:

    CPUID returns value latched at reset,
    not APIC ID register's current value.

Some clustered systems have BIOS-modified APIC IDs.

Therefore use:

    hard_smp_processor_id()


Flow:

hardware APIC ID
      |
      v
shift right by index_msb
      |
      v
physical package ID


===============================================================================
COMPLETE FLOW
===============================================================================

Boot CPU / Secondary CPU APIC Setup
      |
      v
Generic APIC code selects apic_cluster
      |
      v
setup_local_APIC()
      |
      v
apic_cluster.init_apic_ldr()
      |
      +--> read physical APIC ID
      |
      +--> compute cluster ID
      |
      +--> choose logical bit
      |
      +--> write APIC_DFR
      |
      +--> write APIC_LDR
      |
      v
CPU ready for APIC delivery


Runtime IPI
      |
      v
send_IPI_allbutself()
      |
      v
apic_cluster.send_IPI_allbutself()
      |
      +--> build CPU mask
      |
      +--> send_IPI_mask_sequence()
      |
      v
target CPUs receive interrupt


Runtime IRQ Targeting
      |
      v
cluster_cpu_mask_to_apicid()
      |
      v
choose first CPU in mask
      |
      v
return physical APIC ID


===============================================================================
RELATION TO apic.c
===============================================================================

apic.c has generic LAPIC setup:

    setup_local_APIC()

Inside it, architecture-specific APIC mode hooks are called.

For clustered mode:

    init_apic_ldr()
        |
        v
    cluster_init_apic_ldr()


So:

    apic.c
        = generic LAPIC setup

    genapic_cluster.c
        = clustered APIC policy


===============================================================================
RELATION TO entry.S
===============================================================================

entry.S defines interrupt entry points:

    reschedule_interrupt
    call_function_interrupt
    apic_timer_interrupt

This file defines how IPIs get sent:

    send_IPI_all()
    send_IPI_allbutself()
    send_IPI_mask()


Flow:

CPU0 wants CPU1 to reschedule
      |
      v
cluster_send_IPI_mask(CPU1, RESCHEDULE_VECTOR)
      |
      v
CPU1 receives vector
      |
      v
entry.S: reschedule_interrupt
      |
      v
smp_reschedule_interrupt()


===============================================================================
KEY IDEA
===============================================================================

This file is a policy module for APIC delivery on large clustered systems.

It tells generic APIC code:

    - how to initialize clustered logical APIC IDs
    - which CPUs should initially receive IRQs
    - how to allocate interrupt vectors
    - how to send IPIs
    - how to convert CPU masks into APIC IDs
    - how to compute physical package IDs


In simple words:

    apic.c knows how to operate LAPIC hardware.

    genapic_cluster.c knows how to address CPUs
    on clustered APIC machines.


Without this file, Linux would not know the correct APIC addressing rules
for large x86-64 clustered systems.
===============================================================================
```

