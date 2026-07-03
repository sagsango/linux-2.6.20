```c
/*
 * FILE: arch/x86_64/mm/numa.c
 *
 * PURPOSE:
 *   Generic VM initialization for x86-64 NUMA setups.
 *
 * BIG IDEA:
 *   This file converts discovered NUMA memory layout into Linux memory
 *   management structures:
 *
 *      hardware/firmware NUMA info
 *              ↓
 *      bootnode ranges
 *              ↓
 *      pglist_data / pgdat per node
 *              ↓
 *      bootmem allocator per node
 *              ↓
 *      struct page array per node
 *              ↓
 *      zones
 *              ↓
 *      buddy allocator
 */

/*
 * ============================================================
 * 1. BACKGROUND
 * ============================================================
 *
 * NUMA = Non-Uniform Memory Access.
 *
 * In UMA:
 *
 *      CPU0 ----\
 *      CPU1 -----+---- same memory
 *      CPU2 ----/
 *
 * All CPUs have roughly equal memory access cost.
 *
 *
 * In NUMA:
 *
 *      Node0: CPU0 CPU1 + local memory
 *      Node1: CPU2 CPU3 + local memory
 *
 * CPU0 accessing Node0 memory is fast.
 * CPU0 accessing Node1 memory is slower.
 *
 * So Linux wants:
 *
 *      CPU on Node0 → allocate memory from Node0
 *      CPU on Node1 → allocate memory from Node1
 *
 * This file prepares that mapping.
 */

/*
 * ============================================================
 * 2. WHERE THIS FILE RUNS IN BOOT
 * ============================================================
 *
 * Boot flow:
 *
 *      BIOS/UEFI
 *          ↓
 *      bootloader
 *          ↓
 *      head.S
 *          ↓
 *      start_kernel()
 *          ↓
 *      setup_arch()
 *          ↓
 *      numa_initmem_init()
 *          ↓
 *      paging_init()
 *          ↓
 *      buddy allocator ready
 *
 *
 * This file mainly handles:
 *
 *      numa_initmem_init()
 *      setup_node_bootmem()
 *      setup_node_zones()
 *      paging_init()
 */

/*
 * ============================================================
 * 3. IMPORTANT GLOBAL DATA
 * ============================================================
 */

/*
 * One pgdat per NUMA node.
 */
struct pglist_data *node_data[MAX_NUMNODES];

/*
 * Meaning:
 *
 *      node_data[0] ---> pgdat for Node0
 *      node_data[1] ---> pgdat for Node1
 *
 * pgdat contains:
 *
 *      node_start_pfn
 *      node_spanned_pages
 *      node_mem_map
 *      zones
 *      bootmem data
 */


/*
 * One bootmem allocator per node.
 */
bootmem_data_t plat_node_bdata[MAX_NUMNODES];

/*
 * bootmem is the old early boot memory allocator.
 *
 * Before buddy allocator is ready:
 *
 *      use bootmem
 *
 * After paging_init/free_area_init_nodes:
 *
 *      use buddy allocator
 */


/*
 * Physical-address-to-node hash.
 */
struct memnode memnode;

/*
 * Goal:
 *
 *      physical address → NUMA node
 *
 * Fast lookup:
 *
 *      node = memnodemap[physical_address >> memnode_shift]
 */


/*
 * CPU to NUMA node.
 */
unsigned char cpu_to_node[NR_CPUS] = {
        [0 ... NR_CPUS-1] = NUMA_NO_NODE
};

/*
 * Example:
 *
 *      CPU0 → Node0
 *      CPU1 → Node0
 *      CPU2 → Node1
 */


/*
 * APIC ID to NUMA node.
 */
unsigned char apicid_to_node[MAX_LOCAL_APIC] = {
        [0 ... MAX_LOCAL_APIC-1] = NUMA_NO_NODE
};

/*
 * Hardware usually identifies CPUs by APIC ID.
 *
 * Later:
 *
 *      CPU → APIC ID → Node
 */


/*
 * Node to CPU mask.
 */
cpumask_t node_to_cpumask[MAX_NUMNODES];

/*
 * Reverse mapping:
 *
 *      Node0 → CPU0, CPU1
 *      Node1 → CPU2, CPU3
 */


/*
 * Boot parameter flag.
 */
int numa_off __initdata;

/*
 * Set by:
 *
 *      numa=off
 */

/*
 * ============================================================
 * 4. populate_memnodemap()
 * ============================================================
 *
 * Purpose:
 *   Given node memory ranges and a shift value, try to fill memnodemap[].
 *
 * Return:
 *
 *      1   success
 *      0   memnodemap[] too small, shift too small
 *     -1   overlap/lost RAM, shift too big or invalid
 */

static int __init
populate_memnodemap(const struct bootnode *nodes, int numnodes, int shift)
{
        int i;
        int res = -1;
        unsigned long addr, end;

        if (shift >= 64)
                return -1;

        memset(memnodemap, 0xff, sizeof(memnodemap));

        for (i = 0; i < numnodes; i++) {
                addr = nodes[i].start;
                end = nodes[i].end;

                if (addr >= end)
                        continue;

                if ((end >> shift) >= NODEMAPSIZE)
                        return 0;

                do {
                        if (memnodemap[addr >> shift] != 0xff)
                                return -1;

                        memnodemap[addr >> shift] = i;
                        addr += (1UL << shift);
                } while (addr < end);

                res = 1;
        }

        return res;
}

/*
 * Explanation:
 *
 * Suppose:
 *
 *      Node0 = 0GB - 1GB
 *      Node1 = 1GB - 2GB
 *
 * If:
 *
 *      shift = 30
 *
 * Then:
 *
 *      1 << 30 = 1GB
 *
 * memnodemap becomes:
 *
 *      memnodemap[0] = Node0
 *      memnodemap[1] = Node1
 *
 *
 * Lookup:
 *
 *      phys = 0x40000000
 *
 *      index = phys >> 30
 *            = 1
 *
 *      node = memnodemap[1]
 *           = Node1
 *
 *
 * Why can it fail?
 *
 * Case 1:
 *      Table too small:
 *
 *          end >> shift >= NODEMAPSIZE
 *
 * Case 2:
 *      Two nodes map to same slot:
 *
 *          memnodemap[index] already filled
 */

/*
 * ============================================================
 * 5. compute_hash_shift()
 * ============================================================
 *
 * Purpose:
 *   Find the best memnode_shift for physical-address-to-node lookup.
 */

int __init compute_hash_shift(struct bootnode *nodes, int numnodes)
{
        int shift = 20;

        while (populate_memnodemap(nodes, numnodes, shift + 1) >= 0)
                shift++;

        printk(KERN_DEBUG "NUMA: Using %d for the hash shift.\n", shift);

        if (populate_memnodemap(nodes, numnodes, shift) != 1) {
                printk(KERN_INFO
                "Your memory is not aligned you need to rebuild your kernel "
                "with a bigger NODEMAPSIZE shift=%d\n", shift);
                return -1;
        }

        return shift;
}

/*
 * Explanation:
 *
 * It starts with:
 *
 *      shift = 20
 *
 * That means:
 *
 *      1 << 20 = 1MB granularity
 *
 * Then it keeps trying bigger shifts.
 *
 * Bigger shift:
 *
 *      fewer table entries
 *      coarser mapping
 *
 * Smaller shift:
 *
 *      more precise
 *      larger memnodemap needed
 *
 *
 * Goal:
 *
 *      use largest shift that still represents all node ranges correctly.
 */

/*
 * ============================================================
 * 6. early_pfn_to_nid()
 * ============================================================
 */

#ifdef CONFIG_SPARSEMEM
int early_pfn_to_nid(unsigned long pfn)
{
        return phys_to_nid(pfn << PAGE_SHIFT);
}
#endif

/*
 * Used by sparsemem.
 *
 * PFN means Page Frame Number.
 *
 * Convert:
 *
 *      PFN → physical address
 *
 * using:
 *
 *      pfn << PAGE_SHIFT
 *
 * Then:
 *
 *      physical address → NUMA node
 */

/*
 * ============================================================
 * 7. early_node_mem()
 * ============================================================
 *
 * Purpose:
 *   Allocate early boot memory for node metadata.
 *
 * Used for:
 *
 *      pgdat allocation
 *      bootmem bitmap allocation
 */

static void * __init
early_node_mem(int nodeid, unsigned long start, unsigned long end,
               unsigned long size)
{
        unsigned long mem = find_e820_area(start, end, size);
        void *ptr;

        if (mem != -1L)
                return __va(mem);

        ptr = __alloc_bootmem_nopanic(size,
                                      SMP_CACHE_BYTES,
                                      __pa(MAX_DMA_ADDRESS));

        if (ptr == 0) {
                printk(KERN_ERR "Cannot find %lu bytes in node %d\n",
                       size, nodeid);
                return NULL;
        }

        return ptr;
}

/*
 * Flow:
 *
 *      early_node_mem()
 *          |
 *          v
 *      try find_e820_area(start, end, size)
 *          |
 *          +-- success:
 *          |       return __va(physical_address)
 *          |
 *          +-- fail:
 *                  use __alloc_bootmem_nopanic()
 *
 *
 * Why not kmalloc?
 *
 *      kmalloc is not fully ready yet.
 *
 * Why use E820?
 *
 *      E820 tells which physical ranges are usable RAM.
 */

/*
 * ============================================================
 * 8. setup_node_bootmem()
 * ============================================================
 *
 * Purpose:
 *   Initialize bootmem allocator for one NUMA node.
 *
 * This is one of the most important functions in this file.
 */

void __init setup_node_bootmem(int nodeid, unsigned long start, unsigned long end)
{
        unsigned long start_pfn, end_pfn;
        unsigned long bootmap_pages, bootmap_size, bootmap_start;
        unsigned long nodedata_phys;
        void *bootmap;
        const int pgdat_size = round_up(sizeof(pg_data_t), PAGE_SIZE);

        start = round_up(start, ZONE_ALIGN);

        printk(KERN_INFO "Bootmem setup node %d %016lx-%016lx\n",
               nodeid, start, end);

        start_pfn = start >> PAGE_SHIFT;
        end_pfn = end >> PAGE_SHIFT;

        node_data[nodeid] = early_node_mem(nodeid, start, end, pgdat_size);
        if (node_data[nodeid] == NULL)
                return;

        nodedata_phys = __pa(node_data[nodeid]);

        memset(NODE_DATA(nodeid), 0, sizeof(pg_data_t));
        NODE_DATA(nodeid)->bdata = &plat_node_bdata[nodeid];
        NODE_DATA(nodeid)->node_start_pfn = start_pfn;
        NODE_DATA(nodeid)->node_spanned_pages = end_pfn - start_pfn;

        bootmap_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
        bootmap_start = round_up(nodedata_phys + pgdat_size, PAGE_SIZE);

        bootmap = early_node_mem(nodeid, bootmap_start, end,
                                 bootmap_pages << PAGE_SHIFT);

        if (bootmap == NULL) {
                if (nodedata_phys < start || nodedata_phys >= end)
                        free_bootmem((unsigned long)node_data[nodeid],
                                     pgdat_size);

                node_data[nodeid] = NULL;
                return;
        }

        bootmap_start = __pa(bootmap);

        bootmap_size = init_bootmem_node(NODE_DATA(nodeid),
                                         bootmap_start >> PAGE_SHIFT,
                                         start_pfn,
                                         end_pfn);

        free_bootmem_with_active_regions(nodeid, end);

        reserve_bootmem_node(NODE_DATA(nodeid),
                             nodedata_phys,
                             pgdat_size);

        reserve_bootmem_node(NODE_DATA(nodeid),
                             bootmap_start,
                             bootmap_pages << PAGE_SHIFT);

#ifdef CONFIG_ACPI_NUMA
        srat_reserve_add_area(nodeid);
#endif

        node_set_online(nodeid);
}

/*
 * Detailed flow:
 *
 *      setup_node_bootmem(nodeid, start, end)
 *          |
 *          v
 *      align start to ZONE_ALIGN
 *          |
 *          v
 *      convert start/end to PFN
 *          |
 *          v
 *      allocate pgdat for this node
 *          |
 *          v
 *      initialize pgdat:
 *
 *          NODE_DATA(nodeid)->bdata
 *          NODE_DATA(nodeid)->node_start_pfn
 *          NODE_DATA(nodeid)->node_spanned_pages
 *
 *          |
 *          v
 *      calculate bootmem bitmap size
 *          |
 *          v
 *      allocate bootmem bitmap
 *          |
 *          v
 *      init_bootmem_node()
 *          |
 *          v
 *      free usable E820 active regions into bootmem
 *          |
 *          v
 *      reserve pgdat and bootmem bitmap
 *          |
 *          v
 *      mark node online
 *
 *
 * Memory after this:
 *
 *      Node memory:
 *
 *      +----------------------------------+
 *      | pgdat                            |
 *      +----------------------------------+
 *      | bootmem bitmap                   |
 *      +----------------------------------+
 *      | free bootmem pages               |
 *      +----------------------------------+
 *      | reserved firmware / holes        |
 *      +----------------------------------+
 */

/*
 * Key idea:
 *
 *      setup_node_bootmem() does not initialize the final buddy allocator.
 *
 * It initializes the temporary bootmem allocator for the node.
 */

/*
 * ============================================================
 * 9. setup_node_zones()
 * ============================================================
 *
 * Purpose:
 *   Allocate final struct page array for a node.
 */

void __init setup_node_zones(int nodeid)
{
        unsigned long start_pfn, end_pfn, memmapsize, limit;

        start_pfn = node_start_pfn(nodeid);
        end_pfn = node_end_pfn(nodeid);

        memmapsize = sizeof(struct page) * (end_pfn - start_pfn);
        limit = end_pfn << PAGE_SHIFT;

#ifdef CONFIG_FLAT_NODE_MEM_MAP
        NODE_DATA(nodeid)->node_mem_map =
                __alloc_bootmem_core(NODE_DATA(nodeid)->bdata,
                                     memmapsize,
                                     SMP_CACHE_BYTES,
                                     round_down(limit - memmapsize,
                                                PAGE_SIZE),
                                     limit);
#endif
}

/*
 * Background:
 *
 * Every physical page has one struct page.
 *
 * Example:
 *
 *      1GB RAM
 *      4KB pages
 *
 *      number of pages = 1GB / 4KB
 *                      = 262144
 *
 * So Linux needs:
 *
 *      262144 struct page objects
 *
 *
 * This function allocates that array for each node.
 *
 * The result:
 *
 *      NODE_DATA(nodeid)->node_mem_map
 *
 * points to the node's struct page array.
 */

/*
 * ============================================================
 * 10. numa_init_array()
 * ============================================================
 *
 * Purpose:
 *   Fill missing CPU-to-node mappings.
 */

void __init numa_init_array(void)
{
        int rr, i;

        rr = first_node(node_online_map);

        for (i = 0; i < NR_CPUS; i++) {
                if (cpu_to_node[i] != NUMA_NO_NODE)
                        continue;

                numa_set_node(i, rr);

                rr = next_node(rr, node_online_map);
                if (rr == MAX_NUMNODES)
                        rr = first_node(node_online_map);
        }
}

/*
 * Why needed?
 *
 * Some systems have bad firmware/mainboard NUMA information.
 *
 * Some CPUs may not have known node mapping yet.
 *
 * So Linux assigns unknown CPUs round-robin across online nodes.
 *
 *
 * Example:
 *
 *      online nodes: Node0, Node1
 *
 *      CPU0 unknown → Node0
 *      CPU1 unknown → Node1
 *      CPU2 unknown → Node0
 *      CPU3 unknown → Node1
 */

/*
 * ============================================================
 * 11. NUMA EMULATION
 * ============================================================
 *
 * Enabled by:
 *
 *      CONFIG_NUMA_EMU
 *
 * Boot option:
 *
 *      numa=fake=N
 *
 * Example:
 *
 *      numa=fake=4
 *
 * This pretends one physical machine has N NUMA nodes.
 */

#ifdef CONFIG_NUMA_EMU
static int __init numa_emulation(unsigned long start_pfn,
                                 unsigned long end_pfn)
{
        /*
         * Split physical memory into fake nodes.
         */
}
#endif

/*
 * Example:
 *
 *      RAM = 8GB
 *      numa=fake=4
 *
 * Fake layout:
 *
 *      Node0: 0GB - 2GB
 *      Node1: 2GB - 4GB
 *      Node2: 4GB - 6GB
 *      Node3: 6GB - 8GB
 *
 *
 * Then it does the same normal setup:
 *
 *      compute_hash_shift()
 *      e820_register_active_regions()
 *      setup_node_bootmem()
 *      numa_init_array()
 */

/*
 * ============================================================
 * 12. numa_initmem_init()
 * ============================================================
 *
 * Master NUMA initialization function.
 */

void __init numa_initmem_init(unsigned long start_pfn, unsigned long end_pfn)
{
        int i;

#ifdef CONFIG_NUMA_EMU
        if (numa_fake && !numa_emulation(start_pfn, end_pfn))
                return;
#endif

#ifdef CONFIG_ACPI_NUMA
        if (!numa_off &&
            !acpi_scan_nodes(start_pfn << PAGE_SHIFT,
                             end_pfn << PAGE_SHIFT))
                return;
#endif

#ifdef CONFIG_K8_NUMA
        if (!numa_off &&
            !k8_scan_nodes(start_pfn << PAGE_SHIFT,
                           end_pfn << PAGE_SHIFT))
                return;
#endif

        /*
         * Fallback: fake one node covering all memory.
         */
        memnode_shift = 63;
        memnodemap[0] = 0;

        nodes_clear(node_online_map);
        node_set_online(0);

        for (i = 0; i < NR_CPUS; i++)
                numa_set_node(i, 0);

        node_to_cpumask[0] = cpumask_of_cpu(0);

        e820_register_active_regions(0, start_pfn, end_pfn);

        setup_node_bootmem(0,
                           start_pfn << PAGE_SHIFT,
                           end_pfn << PAGE_SHIFT);
}

/*
 * Discovery priority:
 *
 *      1. NUMA emulation
 *      2. ACPI NUMA
 *      3. AMD K8 NUMA
 *      4. fake single Node0
 *
 *
 * Full flow:
 *
 *      numa_initmem_init()
 *          |
 *          v
 *      numa=fake?
 *          |
 *          +-- yes → numa_emulation()
 *          |
 *          v
 *      ACPI NUMA available?
 *          |
 *          +-- yes → acpi_scan_nodes()
 *          |
 *          v
 *      AMD K8 NUMA available?
 *          |
 *          +-- yes → k8_scan_nodes()
 *          |
 *          v
 *      fallback:
 *
 *          Node0 = all memory
 *          all CPUs = Node0
 */

/*
 * ============================================================
 * 13. numa_set_node()
 * ============================================================
 */

void __cpuinit numa_set_node(int cpu, int node)
{
        cpu_pda(cpu)->nodenumber = node;
        cpu_to_node[cpu] = node;
}

/*
 * Purpose:
 *
 *      assign CPU to NUMA node
 *
 * Updates:
 *
 *      per-CPU PDA
 *      global cpu_to_node[]
 */

/*
 * ============================================================
 * 14. numa_add_cpu()
 * ============================================================
 */

__cpuinit void numa_add_cpu(int cpu)
{
        set_bit(cpu, &node_to_cpumask[cpu_to_node(cpu)]);
}

/*
 * Purpose:
 *
 *      add CPU into node_to_cpumask[node]
 *
 * Example:
 *
 *      cpu_to_node(2) = 1
 *
 * Then:
 *
 *      add CPU2 to node_to_cpumask[1]
 */

/*
 * ============================================================
 * 15. numa_free_all_bootmem()
 * ============================================================
 */

unsigned long __init numa_free_all_bootmem(void)
{
        int i;
        unsigned long pages = 0;

        for_each_online_node(i)
                pages += free_all_bootmem_node(NODE_DATA(i));

        return pages;
}

/*
 * Purpose:
 *
 *      Once buddy allocator is ready, free all remaining bootmem pages.
 *
 * Flow:
 *
 *      for every online node:
 *
 *          free_all_bootmem_node(pgdat)
 *
 *      return total freed pages
 */

/*
 * ============================================================
 * 16. arch_sparse_init()
 * ============================================================
 */

#ifdef CONFIG_SPARSEMEM
static void __init arch_sparse_init(void)
{
        int i;

        for_each_online_node(i)
                memory_present(i, node_start_pfn(i), node_end_pfn(i));

        sparse_init();
}
#endif

/*
 * Purpose:
 *
 *      Tell sparsemem which PFN ranges exist for each node.
 *
 * Then:
 *
 *      sparse_init()
 *
 * initializes sparse memory metadata.
 */

/*
 * ============================================================
 * 17. paging_init()
 * ============================================================
 *
 * Purpose:
 *   Final page allocator initialization.
 */

void __init paging_init(void)
{
        int i;
        unsigned long max_zone_pfns[MAX_NR_ZONES];

        memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

        max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN;
        max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN;
        max_zone_pfns[ZONE_NORMAL] = end_pfn;

        arch_sparse_init();

        for_each_online_node(i)
                setup_node_zones(i);

        free_area_init_nodes(max_zone_pfns);
}

/*
 * Zone meaning:
 *
 *      ZONE_DMA:
 *          low memory for old DMA devices
 *
 *      ZONE_DMA32:
 *          memory addressable by 32-bit DMA devices
 *
 *      ZONE_NORMAL:
 *          normal kernel-managed memory
 *
 *
 * Flow:
 *
 *      paging_init()
 *          |
 *          v
 *      set max PFN for DMA/DMA32/NORMAL
 *          |
 *          v
 *      initialize sparsemem
 *          |
 *          v
 *      allocate node_mem_map for each node
 *          |
 *          v
 *      free_area_init_nodes()
 *          |
 *          v
 *      buddy allocator ready
 */

/*
 * ============================================================
 * 18. numa_setup()
 * ============================================================
 *
 * Parses boot parameter:
 *
 *      numa=
 */

static __init int numa_setup(char *opt)
{
        if (!opt)
                return -EINVAL;

        if (!strncmp(opt, "off", 3))
                numa_off = 1;

#ifdef CONFIG_NUMA_EMU
        if (!strncmp(opt, "fake=", 5)) {
                numa_fake = simple_strtoul(opt + 5, NULL, 0);
                if (numa_fake >= MAX_NUMNODES)
                        numa_fake = MAX_NUMNODES;
        }
#endif

#ifdef CONFIG_ACPI_NUMA
        if (!strncmp(opt, "noacpi", 6))
                acpi_numa = -1;

        if (!strncmp(opt, "hotadd=", 7))
                hotadd_percent = simple_strtoul(opt + 7, NULL, 10);
#endif

        return 0;
}

early_param("numa", numa_setup);

/*
 * Supported options:
 *
 *      numa=off
 *          disable NUMA
 *
 *      numa=fake=N
 *          fake N NUMA nodes
 *
 *      numa=noacpi
 *          disable ACPI NUMA parsing
 *
 *      numa=hotadd=N
 *          reserve memory for ACPI memory hot-add
 *
 *
 * early_param means:
 *
 *      parse this very early during boot
 */

/*
 * ============================================================
 * 19. init_cpu_to_node()
 * ============================================================
 */

void __init init_cpu_to_node(void)
{
        int i;

        for (i = 0; i < NR_CPUS; i++) {
                u8 apicid = x86_cpu_to_apicid[i];

                if (apicid == BAD_APICID)
                        continue;

                if (apicid_to_node[apicid] == NUMA_NO_NODE)
                        continue;

                numa_set_node(i, apicid_to_node[apicid]);
        }
}

/*
 * Purpose:
 *
 *      finalize CPU → node mapping using APIC IDs.
 *
 *
 * Flow:
 *
 *      CPU number
 *          |
 *          v
 *      x86_cpu_to_apicid[cpu]
 *          |
 *          v
 *      apicid_to_node[apicid]
 *          |
 *          v
 *      numa_set_node(cpu, node)
 *
 *
 * This connects hardware CPU identity to Linux NUMA topology.
 */

/*
 * ============================================================
 * 20. pfn_valid()
 * ============================================================
 */

#ifdef CONFIG_DISCONTIGMEM
int pfn_valid(unsigned long pfn)
{
        unsigned nid;

        if (pfn >= num_physpages)
                return 0;

        nid = pfn_to_nid(pfn);

        if (nid == 0xff)
                return 0;

        return pfn >= node_start_pfn(nid) &&
               pfn < node_end_pfn(nid);
}
#endif

/*
 * Purpose:
 *
 *      check whether a PFN is valid physical memory.
 *
 *
 * Flow:
 *
 *      pfn_valid(pfn)
 *          |
 *          v
 *      pfn >= num_physpages?
 *          |
 *          +-- yes → invalid
 *          |
 *          v
 *      nid = pfn_to_nid(pfn)
 *          |
 *          v
 *      nid invalid?
 *          |
 *          +-- yes → invalid
 *          |
 *          v
 *      pfn inside node range?
 *          |
 *          +-- yes → valid
 *          +-- no  → invalid
 */

/*
 * ============================================================
 * 21. FULL SYSTEM FLOW DIAGRAM
 * ============================================================
 *
 *      Firmware / hardware
 *              |
 *              v
 *      E820 memory map
 *              |
 *              v
 *      ACPI SRAT or K8 NUMA scan
 *              |
 *              v
 *      bootnode ranges
 *
 *          Node0: [start0, end0)
 *          Node1: [start1, end1)
 *
 *              |
 *              v
 *      compute_hash_shift()
 *
 *          physical address → node lookup
 *
 *              |
 *              v
 *      setup_node_bootmem()
 *
 *          allocate pgdat
 *          allocate bootmem bitmap
 *          initialize node bootmem
 *
 *              |
 *              v
 *      paging_init()
 *
 *          setup sparsemem
 *          allocate struct page arrays
 *          setup zones
 *          initialize buddy allocator
 *
 *              |
 *              v
 *      numa_free_all_bootmem()
 *
 *          bootmem released
 *
 *              |
 *              v
 *      normal memory allocation
 */

/*
 * ============================================================
 * 22. FINAL MENTAL MODEL
 * ============================================================
 *
 * This file is the bridge between:
 *
 *      "I discovered NUMA nodes"
 *
 * and:
 *
 *      "Linux can allocate pages from NUMA nodes"
 *
 *
 * Before this file:
 *
 *      memory map exists
 *      maybe NUMA topology exists
 *      buddy allocator not ready
 *
 *
 * After this file:
 *
 *      node_data[] exists
 *      bootmem exists per node
 *      CPU-to-node mapping exists
 *      physical-address-to-node lookup exists
 *      struct page arrays exist
 *      zones exist
 *      buddy allocator is ready
 *
 *
 * One-line summary:
 *
 *      numa.c initializes x86-64 NUMA memory management by building
 *      per-node pgdat, bootmem, memnodemap, CPU-node mappings, zones,
 *      and final buddy allocator data structures.
 */
```

