```text
===============================================================================
GENERIC APIC SUBARCH PROBE LAYER
File: arch/x86_64/kernel/genapic.c
===============================================================================

PURPOSE
=======

This file chooses which APIC routing mode Linux should use on this machine.

It does not directly program all APIC registers.

Instead, it selects one APIC policy module:

    apic_flat
    apic_physflat
    apic_cluster


Then generic APIC code uses:

    genapic->function()

to send IPIs, choose IRQ targets, initialize APIC IDs, etc.


===============================================================================
BACKGROUND
==========

Different x86-64 machines need different APIC addressing modes.

Small system:

    flat APIC mode is enough

Large AMD system:

    physical flat APIC mode

Large clustered Intel/IBM system:

    clustered APIC mode


So Linux needs a probe layer:

    Look at CPU/APIC topology
    Pick best APIC mode


===============================================================================
IMPORTANT GLOBALS
===============================================================================

x86_cpu_to_apicid[NR_CPUS]
--------------------------

Maps Linux CPU number to physical APIC ID.

Example:

    CPU0 -> APIC ID 0
    CPU1 -> APIC ID 1
    CPU2 -> APIC ID 2


x86_cpu_to_log_apicid[NR_CPUS]
------------------------------

Maps Linux CPU number to logical APIC ID.

Used by logical APIC modes.


genapic
-------

Current selected APIC mode.

Default:

    genapic = &apic_flat


genapic_force
-------------

Optional forced APIC mode.

Used when quirks or command-line/platform logic require a specific mode.


===============================================================================
AVAILABLE MODES
===============================================================================

1. apic_flat
------------

    logical destination mode
    good for small systems
    usually <= 8 CPUs


2. apic_physflat
----------------

    physical destination mode
    used especially on AMD systems with many CPUs


3. apic_cluster
---------------

    clustered APIC mode
    used for large clustered systems


===============================================================================
MAIN FUNCTION
===============================================================================

Function:

    clustered_apic_check()

Purpose:

    Choose APIC routing mode.


High-level flow:

clustered_apic_check()
      |
      +--> forced mode?
      |
      +--> ACPI forces physical destination?
      |
      +--> count APIC clusters
      |
      +--> AMD special handling
      |
      +--> small flat system?
      |
      +--> otherwise clustered mode
      |
      v
print selected mode


===============================================================================
STEP 1 : FORCED MODE
===============================================================================

Code:

    if (genapic_force) {
        genapic = genapic_force;
        goto print;
    }


Meaning:

    If some earlier logic forced an APIC mode, trust it.

Example:

    genapic_force = &apic_cluster

Then:

    use clustered mode immediately.


===============================================================================
STEP 2 : ACPI FORCE PHYSICAL DESTINATION MODE
===============================================================================

Code idea:

    if FADT revision > 2
        if force_apic_physical_destination_mode
            genapic = &apic_cluster


Meaning:

    ACPI firmware may tell Linux:

        Use physical APIC destination mode.


Example:

    Certain x86_64 ES7000 machines need this.


===============================================================================
STEP 3 : COUNT APIC IDs AND CLUSTERS
===============================================================================

Loop:

for each possible CPU:
    id = bios_cpu_apicid[i]

    if valid:
        update max_apic
        cluster_cnt[APIC_CLUSTERID(id)]++


Purpose:

    Find how CPUs are distributed across APIC clusters.


Example:

bios_cpu_apicid:

    CPU0 -> 0x00
    CPU1 -> 0x01
    CPU2 -> 0x02
    CPU3 -> 0x03

Then:

    cluster 0 has 4 CPUs


Example clustered:

    CPU0 -> 0x00
    CPU1 -> 0x01
    CPU2 -> 0x10
    CPU3 -> 0x11

Then:

    cluster 0 has 2 CPUs
    cluster 1 has 2 CPUs


===============================================================================
STEP 4 : AMD SPECIAL CASE
===============================================================================

Code:

    if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD) {
        genapic = &apic_physflat;

        if no CPU hotplug:
            if max_apic <= 8:
                genapic = &apic_flat;

        goto print;
    }


Meaning:

    AMD avoids clustered APIC mode.

Small AMD system:

    use flat mode

Large AMD system:

    use physflat mode


Why?

Flat logical mode has limited APIC mask bits.

If APIC IDs exceed 8, logical mask may overflow.

Physical mode avoids this.


===============================================================================
CPU HOTPLUG NOTE
===============================================================================

If CONFIG_HOTPLUG_CPU is enabled:

    avoid APIC broadcast shortcuts

Why?

CPU removal can race with broadcast delivery.

So kernel prefers physflat in that case.


===============================================================================
STEP 5 : COUNT CLUSTERS
===============================================================================

For non-AMD:

    count how many APIC clusters are present

Variables:

    clusters
        number of non-empty clusters

    max_cluster
        largest CPU count in any cluster


Example:

cluster_cnt:

    cluster 0 = 4
    cluster 1 = 0
    cluster 2 = 0

Then:

    clusters = 1
    max_cluster = 4


Example:

    cluster 0 = 4
    cluster 1 = 4

Then:

    clusters = 2
    max_cluster = 4


===============================================================================
STEP 6 : SELECT FLAT OR CLUSTERED
===============================================================================

Condition for flat:

    clusters <= 1
    max_cluster <= 8
    cluster 0 contains all CPUs


Meaning:

    CPUs fit in one flat logical APIC mask.


If true:

    use apic_flat

except with CPU hotplug:

    use apic_physflat


Otherwise:

    use apic_cluster


Decision tree:

                Start
                  |
                  v
          genapic forced?
           |            |
          yes           no
           |            |
           v            v
        forced      ACPI force physical?
                         |
                  +------+------+
                  |             |
                 yes            no
                  |             |
                  v             v
              cluster      AMD CPU?
                              |
                       +------+------+
                       |             |
                      yes            no
                       |             |
                       v             v
              max_apic <= 8?    count clusters
                  |                  |
            +-----+-----+            |
            |           |            |
           yes          no           |
            |           |            |
          flat      physflat         |
                                     v
                         one cluster and <=8 CPUs?
                              |
                         +----+----+
                         |         |
                        yes        no
                         |         |
                       flat     cluster


===============================================================================
SEND IPI TO SELF
===============================================================================

Function:

    send_IPI_self(int vector)

Code:

    __send_IPI_shortcut(APIC_DEST_SELF, vector, APIC_DEST_PHYSICAL);


Meaning:

    Send an interrupt from this CPU to itself.


Used for:

    self-reschedule
    local APIC testing
    generic IPI paths


Flow:

CPU0
 |
 +--> APIC shortcut SELF
 |
 v
CPU0 receives vector


===============================================================================
HOW THIS CONNECTS TO OTHER APIC FILES
===============================================================================

genapic.c
---------

Chooses mode:

    genapic = &apic_flat
    genapic = &apic_physflat
    genapic = &apic_cluster


genapic_flat.c
--------------

Implements:

    flat logical mode
    physical flat mode


genapic_cluster.c
-----------------

Implements:

    clustered APIC mode


apic.c
------

Uses selected genapic operations.

Example:

    init_apic_ldr()
        |
        v
    genapic->init_apic_ldr()


smp.c / ipi.c
-------------

Use:

    genapic->send_IPI_mask()
    genapic->send_IPI_all()
    genapic->send_IPI_allbutself()


===============================================================================
COMPLETE BOOT FLOW
===============================================================================

Boot
 |
 v
BIOS/ACPI reports APIC IDs
 |
 v
bios_cpu_apicid[]
 |
 v
clustered_apic_check()
 |
 +--> inspect APIC ID topology
 |
 +--> inspect vendor
 |
 +--> inspect ACPI flags
 |
 +--> select APIC mode
 |
 v
genapic = selected mode
 |
 v
setup_local_APIC()
 |
 v
genapic->init_apic_ldr()
 |
 v
APIC routing ready


===============================================================================
RUNTIME FLOW
===============================================================================

Kernel wants to send IPI:

    send_IPI_mask(mask, vector)

Generic path:

    genapic->send_IPI_mask(mask, vector)

If selected mode is flat:

    flat_send_IPI_mask()

If selected mode is physflat:

    physflat_send_IPI_mask()

If selected mode is clustered:

    cluster_send_IPI_mask()


===============================================================================
KEY IDEA
===============================================================================

This file is the APIC mode selector.

It answers:

    "Which APIC addressing model matches this machine?"


Small machines:

    flat

Large AMD / hotplug-sensitive machines:

    physflat

Clustered systems:

    cluster


After this decision, the rest of the kernel uses genapic function pointers
and does not need to care about the exact APIC routing mode.
===============================================================================
```

