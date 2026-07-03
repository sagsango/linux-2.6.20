```text
FILE: arch/x86_64/mm/k8topology.c
TOPIC: AMD K8 NUMA discovery from Northbridge PCI config space

============================================================
1. BACKGROUND
============================================================

This file is for early AMD Opteron / K8 NUMA systems.

On those systems, memory is attached to CPU sockets/nodes. Each node has
its own local memory controller.

So Linux needs to discover:

    Node 0 owns which physical memory range?
    Node 1 owns which physical memory range?
    Node 2 owns which physical memory range?
    ...

This code reads that information directly from the AMD K8 Northbridge.

============================================================
2. WHY NORTHBRIDGE?
============================================================

On AMD K8, the memory controller is integrated with the CPU/node.

The Northbridge PCI config registers describe:

    - how many NUMA nodes exist
    - memory base for each node
    - memory limit for each node
    - node ID
    - whether memory interleaving is enabled

So instead of getting NUMA info from ACPI SRAT, this code reads hardware
registers directly.

============================================================
3. BIG FLOW
============================================================

    k8_scan_nodes(start, end)
        |
        v
    check early PCI access allowed
        |
        v
    find AMD K8 Northbridge
        |
        v
    read number of nodes
        |
        v
    for each possible node entry 0..7:
        |
        +--> read base register
        +--> read limit register
        +--> extract node id
        +--> skip disabled/bad/interleaved entries
        +--> convert register encoding to physical address
        +--> clamp to valid memory range
        +--> store node start/end
        +--> register active E820 regions
        |
        v
    compute memnode hash
        |
        v
    setup APIC-ID-to-node mapping
        |
        v
    setup bootmem for each node
        |
        v
    numa_init_array()

============================================================
4. MAIN PURPOSE
============================================================

By the end of this function, Linux knows:

    physical address range -> NUMA node

Example:

    0x00000000 - 0x3fffffff  -> Node 0
    0x40000000 - 0x7fffffff  -> Node 1

Then the kernel can allocate memory close to the CPU using it.

============================================================
5. FUNCTION: find_northbridge()
============================================================

Code:

    for (num = 0; num < 32; num++) {
        header = read_pci_config(0, num, 0, 0x00);
        if (header != (PCI_VENDOR_ID_AMD | (0x1100<<16)))
            continue;

        header = read_pci_config(0, num, 1, 0x00);
        if (header != (PCI_VENDOR_ID_AMD | (0x1101<<16)))
            continue;

        return num;
    }

It scans PCI bus 0, device numbers 0..31.

It looks for AMD K8 Northbridge functions:

    function 0 device ID 0x1100
    function 1 device ID 0x1101

If both are present on the same PCI device number, that device is treated
as the K8 Northbridge.

Flow:

    PCI bus 0
      |
      +-- dev 0
      +-- dev 1
      +-- dev 2
      ...
      +-- dev 31
             |
             v
        check function 0 vendor/device
        check function 1 vendor/device

Return:

    device number if found
    -1 if not found

============================================================
6. WHY EARLY PCI?
============================================================

Code:

    if (!early_pci_allowed())
        return -1;

This runs very early during boot.

Normal PCI subsystem may not be initialized yet.

So this code uses early direct PCI config access:

    read_pci_config(bus, dev, func, offset)

If early PCI access is not safe/allowed, it gives up.

============================================================
7. FUNCTION: k8_scan_nodes()
============================================================

Signature:

    int __init k8_scan_nodes(unsigned long start, unsigned long end)

Meaning:

    Discover NUMA node memory layout between physical range:

        start ... end

__init means:

    this code is needed only during boot
    memory can be freed after boot

============================================================
8. IMPORTANT LOCAL VARIABLES
============================================================

    struct bootnode nodes[8];

Stores discovered memory ranges.

Each node has:

    nodes[i].start
    nodes[i].end

AMD K8 supports up to 8 node entries here.

------------------------------------------------------------

    unsigned char nodeids[8];

Stores hardware node IDs read from registers.

------------------------------------------------------------

    nodemask_t nodes_parsed;

Tracks which node IDs have already been seen.

Prevents duplicate node entries.

------------------------------------------------------------

    unsigned dualcore = 0;

Used later to map APIC IDs to NUMA nodes.

For dual-core K8, APIC ID layout differs.

------------------------------------------------------------

    unsigned numnodes;

Number of nodes reported by Northbridge.

============================================================
9. READ NUMBER OF NODES
============================================================

Code:

    reg = read_pci_config(0, nb, 0, 0x60);
    numnodes = ((reg >> 4) & 0xF) + 1;

Register 0x60 contains node count information.

This extracts bits:

    bits 7:4

Then adds 1.

Example:

    field = 0 -> 1 node
    field = 1 -> 2 nodes
    field = 3 -> 4 nodes

============================================================
10. MAIN LOOP OVER 8 NODE ENTRIES
============================================================

Code:

    for (i = 0; i < 8; i++) {
        ...
    }

Each entry describes a possible memory range/node.

For each i:

    base  register = 0x40 + i*8
    limit register = 0x44 + i*8

So:

    entry 0: base 0x40, limit 0x44
    entry 1: base 0x48, limit 0x4c
    entry 2: base 0x50, limit 0x54
    ...

These are read from PCI function 1:

    read_pci_config(0, nb, 1, offset)

============================================================
11. DUAL CORE CHECK
============================================================

Code:

    dualcore |= ((read_pci_config(0, nb, 3, 0xe8) >> 12) & 3) == 1;

This checks a K8 register to detect dual-core configuration.

Result:

    dualcore = 0 or 1

Later used here:

    apicid_to_node[nodeid << dualcore] = i;
    apicid_to_node[(nodeid << dualcore) + dualcore] = i;

If dualcore == 0:

    nodeid << 0 = nodeid

If dualcore == 1:

    nodeid << 1 = nodeid * 2

So one node may correspond to APIC IDs:

    nodeid*2
    nodeid*2 + 1

============================================================
12. READ BASE AND LIMIT
============================================================

Code:

    base = read_pci_config(0, nb, 1, 0x40 + i*8);
    limit = read_pci_config(0, nb, 1, 0x44 + i*8);

These are hardware-encoded memory range registers.

They are not plain byte addresses yet.

The code later converts them into byte addresses.

============================================================
13. EXTRACT NODE ID
============================================================

Code:

    nodeid = limit & 7;
    nodeids[i] = nodeid;

The lower 3 bits of limit register contain node ID.

So possible node IDs:

    0..7

============================================================
14. SKIP DISABLED NODE ENTRY
============================================================

Code:

    if ((base & 3) == 0) {
        if (i < numnodes)
            printk("Skipping disabled node %d\n", i);
        continue;
    }

The low bits of base contain enable/valid information.

If base & 3 == 0:

    this memory range is disabled

So skip it.

============================================================
15. IGNORE EXCESS NODE ID
============================================================

Code:

    if (nodeid >= numnodes) {
        printk("Ignoring excess node %d\n", nodeid, base, limit);
        continue;
    }

If hardware entry says node ID is outside real node count, ignore it.

Example:

    numnodes = 2

Valid node IDs:

    0, 1

If entry says:

    nodeid = 4

skip it.

============================================================
16. SKIP EMPTY LIMIT
============================================================

Code:

    if (!limit) {
        printk("Skipping node entry %d", i);
        continue;
    }

A zero limit means no useful memory range.

============================================================
17. REJECT INTERLEAVING MODE
============================================================

Code:

    if ((base >> 8) & 3 || (limit >> 8) & 3) {
        printk("Node %d using interleaving mode");
        return -1;
    }

This code does not support node memory interleaving.

Memory interleaving means physical addresses are striped across nodes.

Example:

    cache line/page chunks alternate between node 0 and node 1

This makes simple range mapping harder:

    base..limit -> node

is no longer enough.

So if interleaving bits are set, this code gives up.

============================================================
18. DUPLICATE NODE CHECK
============================================================

Code:

    if (node_isset(nodeid, nodes_parsed)) {
        printk("Node %d already present. Skipping\n", nodeid);
        continue;
    }

Each node should appear once.

If duplicate entry exists, skip duplicate.

============================================================
19. CONVERT LIMIT REGISTER TO BYTE ADDRESS
============================================================

Code:

    limit >>= 16;
    limit <<= 24;
    limit |= (1<<24)-1;
    limit++;

This converts encoded K8 limit register into an exclusive end address.

Step-by-step:

    limit >>= 16

Take high encoded address bits.

    limit <<= 24

Convert to byte address granularity.

    limit |= (1<<24)-1

Set lower 24 bits to 1.

This makes inclusive end:

    ...ffffff

Then:

    limit++

Convert inclusive limit to exclusive end.

So:

    [base, limit)

This is the normal Linux range style.

============================================================
20. CLAMP LIMIT TO END_PFN
============================================================

Code:

    if (limit > end_pfn << PAGE_SHIFT)
        limit = end_pfn << PAGE_SHIFT;

Do not allow node memory past actual known RAM end.

    end_pfn << PAGE_SHIFT

means:

    last physical RAM end address

============================================================
21. CHECK LIMIT <= BASE
============================================================

Code:

    if (limit <= base)
        continue;

Still using old encoded base here, but shortly base will be converted.
This protects against obviously invalid ranges.

============================================================
22. CONVERT BASE REGISTER TO BYTE ADDRESS
============================================================

Code:

    base >>= 16;
    base <<= 24;

Similar conversion.

It extracts encoded high bits and converts to byte address.

So hardware encoding becomes:

    actual physical base address

============================================================
23. CLAMP TO REQUESTED START/END
============================================================

Code:

    if (base < start)
        base = start;

    if (limit > end)
        limit = end;

This restricts discovered node ranges to caller-provided scan range.

So final node range is inside:

    [start, end)

============================================================
24. EMPTY / BOGUS RANGE CHECKS
============================================================

Code:

    if (limit == base) {
        printk("Empty node %d\n", nodeid);
        continue;
    }

    if (limit < base) {
        printk("Node %d bogus settings\n");
        continue;
    }

Invalid ranges are skipped.

============================================================
25. SORTING CHECK
============================================================

Code:

    if (prevbase > base) {
        printk("Node map not sorted");
        return -1;
    }

This expects node ranges to appear sorted by physical base address.

Example valid:

    node 0: 0x00000000 - 0x40000000
    node 1: 0x40000000 - 0x80000000

Example invalid:

    node 0: 0x40000000 - 0x80000000
    node 1: 0x00000000 - 0x40000000

It could sort, but this old code does not.

============================================================
26. STORE NODE RANGE
============================================================

Code:

    nodes[nodeid].start = base;
    nodes[nodeid].end = limit;

Now Linux has:

    nodeid -> physical memory range

Example:

    nodes[0].start = 0x00000000
    nodes[0].end   = 0x40000000

============================================================
27. REGISTER ACTIVE E820 REGIONS
============================================================

Code:

    e820_register_active_regions(nodeid,
        nodes[nodeid].start >> PAGE_SHIFT,
        nodes[nodeid].end >> PAGE_SHIFT);

This tells Linux memory management:

    these PFNs belong to this NUMA node

PFN means Page Frame Number.

Conversion:

    physical address >> PAGE_SHIFT

Example:

    0x40000000 >> 12 = PFN 0x40000

============================================================
28. MARK NODE PARSED
============================================================

Code:

    node_set(nodeid, nodes_parsed);

This records that this node ID has been handled.

============================================================
29. IF NO NODES FOUND
============================================================

Code:

    if (!found)
        return -1;

If no valid NUMA memory ranges were discovered, K8 NUMA scan fails.

Kernel may fall back to another NUMA method or non-NUMA setup.

============================================================
30. COMPUTE HASH SHIFT
============================================================

Code:

    memnode_shift = compute_hash_shift(nodes, 8);

Linux needs fast lookup:

    physical address -> node ID

Instead of scanning all node ranges every time, it builds a hash function.

The hash shift helps convert PFN/address into node index quickly.

Conceptually:

    node = memnode_map[phys_addr >> memnode_shift]

If no usable hash shift exists:

    return -1

============================================================
31. SETUP APIC ID TO NODE MAP
============================================================

Code:

    apicid_to_node[nodeid << dualcore] = i;
    apicid_to_node[(nodeid << dualcore) + dualcore] = i;

This maps CPU/APIC IDs to NUMA nodes.

Why?

Scheduler needs to know:

    CPU 0 belongs to node 0
    CPU 1 belongs to node 0
    CPU 2 belongs to node 1
    ...

For dual-core:

    APIC IDs may be:

        node 0 core 0 -> APIC 0
        node 0 core 1 -> APIC 1
        node 1 core 0 -> APIC 2
        node 1 core 1 -> APIC 3

So shifting handles that layout.

============================================================
32. SETUP NODE BOOTMEM
============================================================

Code:

    setup_node_bootmem(i, nodes[i].start, nodes[i].end);

This initializes early memory allocator data for each NUMA node.

Before the buddy allocator is fully ready, old Linux uses bootmem.

This tells bootmem:

    node i owns memory range [start, end)

Later normal page allocator uses node zones.

============================================================
33. NUMA_INIT_ARRAY()
============================================================

Code:

    numa_init_array();

This finalizes NUMA arrays.

It fills missing/default node mappings and prepares global NUMA data.

============================================================
34. COMPLETE FLOW DIAGRAM
============================================================

    k8_scan_nodes(start, end)
        |
        v
    nodes_clear(nodes_parsed)
        |
        v
    early_pci_allowed()?
        |
        +-- no -----------------------------> return -1
        |
        v
    find_northbridge()
        |
        +-- not found ----------------------> return -1
        |
        v
    read PCI config 0x60
        |
        v
    numnodes = encoded_count + 1
        |
        v
    for i = 0..7:
        |
        +--> read base register
        |
        +--> read limit register
        |
        +--> nodeid = limit & 7
        |
        +--> base disabled?
        |       |
        |       +-- yes --------------------> continue
        |
        +--> nodeid >= numnodes?
        |       |
        |       +-- yes --------------------> continue
        |
        +--> limit == 0?
        |       |
        |       +-- yes --------------------> continue
        |
        +--> interleaving enabled?
        |       |
        |       +-- yes --------------------> return -1
        |
        +--> duplicate node?
        |       |
        |       +-- yes --------------------> continue
        |
        +--> convert limit to byte end
        |
        +--> clamp limit to RAM end
        |
        +--> convert base to byte start
        |
        +--> clamp base/end to caller range
        |
        +--> empty/bogus range?
        |       |
        |       +-- yes --------------------> continue
        |
        +--> unsorted?
        |       |
        |       +-- yes --------------------> return -1
        |
        +--> nodes[nodeid] = [base, limit)
        |
        +--> e820_register_active_regions()
        |
        +--> mark node parsed
        |
        v
    found == 0?
        |
        +-- yes ----------------------------> return -1
        |
        v
    compute_hash_shift(nodes)
        |
        +-- fail ---------------------------> return -1
        |
        v
    for each valid node:
        |
        +--> setup apicid_to_node[]
        |
        +--> setup_node_bootmem()
        |
        v
    numa_init_array()
        |
        v
    return 0

============================================================
35. EXAMPLE
============================================================

Suppose hardware reports:

    numnodes = 2

Node register entries decode to:

    node 0:
        base  = 0x00000000
        limit = 0x40000000

    node 1:
        base  = 0x40000000
        limit = 0x80000000

Then Linux records:

    nodes[0] = [0x00000000, 0x40000000)
    nodes[1] = [0x40000000, 0x80000000)

E820 active regions:

    node 0 PFN 0x00000 - 0x40000
    node 1 PFN 0x40000 - 0x80000

APIC mapping:

    CPU/APIC 0 -> node 0
    CPU/APIC 1 -> node 0    if dualcore
    CPU/APIC 2 -> node 1
    CPU/APIC 3 -> node 1    if dualcore

============================================================
36. WHY THIS MATTERS
============================================================

Without NUMA discovery, Linux may think memory is uniform.

That would hurt performance.

Bad case:

    CPU on socket 0 frequently allocates memory from socket 1

Result:

    slower memory access
    more HyperTransport traffic
    worse scheduling locality

With this code:

    CPU on node 0 prefers node 0 memory
    CPU on node 1 prefers node 1 memory

============================================================
37. IMPORTANT CONCEPTS
============================================================

Northbridge:

    hardware block exposing memory routing/configuration.

NUMA node:

    CPU/memory locality domain.

E820:

    BIOS-provided physical memory map.

PFN:

    physical page number.

APIC ID:

    CPU interrupt-controller ID, used to identify CPUs.

bootmem:

    old early boot memory allocator.

memnode_shift:

    value used for fast physical-address-to-node lookup.

============================================================
38. ONE-LINE SUMMARY
============================================================

k8_scan_nodes() reads AMD K8 Northbridge PCI registers, decodes each
node's physical memory range, registers those ranges with Linux NUMA
memory management, maps CPUs/APIC IDs to nodes, and initializes per-node
boot memory.
```

