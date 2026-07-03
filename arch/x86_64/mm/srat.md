```
/*
 * FILE: arch/x86_64/mm/srat.c
 *
 * ===============================================================
 *                    ACPI SRAT NUMA INITIALIZATION
 * ===============================================================
 *
 * PURPOSE
 * -------
 * This file discovers the NUMA topology from the ACPI SRAT (System
 * Resource Affinity Table) and SLIT (System Locality Information Table)
 * provided by the BIOS/UEFI firmware.
 *
 * Instead of probing hardware directly (like AMD K8 northbridge code),
 * this file trusts firmware to describe:
 *
 *      • Which CPUs belong to which NUMA node
 *      • Which physical memory belongs to which NUMA node
 *      • Distance between NUMA nodes
 *      • Memory hotplug regions
 *
 * Finally it builds Linux's NUMA data structures.
 *
 *
 * ===============================================================
 *                 WHERE THIS FILE RUNS DURING BOOT
 * ===============================================================
 *
 *                 BIOS / UEFI
 *                      │
 *                      │ Creates ACPI Tables
 *                      │
 *                      ▼
 *                 RSDP
 *                      │
 *                      ▼
 *              XSDT / RSDT
 *                      │
 *          ┌───────────┴───────────┐
 *          │                       │
 *          ▼                       ▼
 *       SRAT Table             SLIT Table
 *          │                       │
 *          ▼                       ▼
 * acpi_numa_processor_affinity()   acpi_numa_slit_init()
 * acpi_numa_memory_affinity()
 *          │
 *          ▼
 *      acpi_scan_nodes()
 *          │
 *          ▼
 * setup_node_bootmem()
 *          │
 *          ▼
 * paging_init()
 *
 *
 * ===============================================================
 *                    WHAT IS SRAT?
 * ===============================================================
 *
 * SRAT = System Resource Affinity Table
 *
 * It contains two major entry types:
 *
 *   1. Processor Affinity Entry
 *
 *      APIC ID  →  Proximity Domain (PXM)
 *
 *   2. Memory Affinity Entry
 *
 *      Physical Memory Range → PXM
 *
 * Example:
 *
 *      CPU0 (APIC 0) ------+
 *                          |
 *      CPU1 (APIC 1) ------+---- PXM 0
 *
 *      Memory:
 *
 *      0GB -------- 8GB  ---> PXM 0
 *
 *
 * Linux converts:
 *
 *      PXM 0
 *          ↓
 *      NUMA Node 0
 *
 *
 * ===============================================================
 *                    WHAT IS SLIT?
 * ===============================================================
 *
 * SLIT = System Locality Information Table
 *
 * It stores NUMA distance information.
 *
 * Example:
 *
 *              Node0  Node1
 *
 * Node0         10      20
 * Node1         20      10
 *
 * 10 = local memory
 * 20 = remote memory
 *
 * Scheduler and memory allocator later use these distances.
 *
 *
 * ===============================================================
 *                MAJOR DATA STRUCTURES
 * ===============================================================
 *
 * nodes[]
 *      Temporary memory ranges discovered from SRAT.
 *
 * nodes_add[]
 *      Memory hotplug regions.
 *
 * nodes_parsed
 *      Bitmap of valid NUMA nodes.
 *
 * apicid_to_node[]
 *      APIC ID → NUMA node mapping.
 *
 * acpi_slit
 *      Pointer to parsed SLIT table.
 *
 *
 * ===============================================================
 *                  IMPORTANT FUNCTIONS
 * ===============================================================
 *
 * setup_node()
 *      Convert ACPI Proximity Domain (PXM)
 *      into Linux NUMA node ID.
 *
 * conflicting_nodes()
 *      Detect overlapping memory ranges.
 *
 * cutoff_node()
 *      Trim node memory to actual RAM limits.
 *
 * bad_srat()
 *      Reject broken BIOS SRAT tables and
 *      fall back to other NUMA methods.
 *
 * slit_valid()
 *      Verify SLIT distance matrix.
 *
 * acpi_numa_processor_affinity_init()
 *      Build:
 *
 *          APIC ID
 *              ↓
 *          NUMA Node
 *
 * acpi_numa_memory_affinity_init()
 *      Build:
 *
 *          Memory Range
 *              ↓
 *          NUMA Node
 *
 * reserve_hotadd()
 *      Reserve memory for future hot-plug.
 *
 * acpi_scan_nodes()
 *      Final validation.
 *
 *      Registers all NUMA nodes with Linux:
 *
 *          setup_node_bootmem()
 *
 * __node_distance()
 *      Returns NUMA distance between nodes.
 *
 *
 * ===============================================================
 *                     COMPLETE FLOW
 * ===============================================================
 *
 *          BIOS/UEFI
 *               │
 *               ▼
 *        ACPI SRAT Table
 *               │
 *               ├──────────────┐
 *               │              │
 *               ▼              ▼
 *        Processor Entry   Memory Entry
 *               │              │
 *               ▼              ▼
 *      APIC → Node       Memory → Node
 *               │              │
 *               └──────┬───────┘
 *                      ▼
 *               acpi_scan_nodes()
 *                      │
 *          Validate overlaps
 *          Validate coverage
 *          Validate SLIT
 *                      │
 *                      ▼
 *            compute_hash_shift()
 *                      │
 *                      ▼
 *          setup_node_bootmem()
 *                      │
 *                      ▼
 *              paging_init()
 *                      │
 *                      ▼
 *          Linux NUMA Ready
 *
 *
 * ===============================================================
 *                     RELATION TO OTHER FILES
 * ===============================================================
 *
 * srat.c
 *      Reads NUMA topology from firmware.
 *
 * numa.c
 *      Creates Linux memory-management structures
 *      using information produced here.
 *
 * pageattr.c
 *      Changes page attributes later during runtime.
 *
 * ioremap.c
 *      Uses pageattr.c for cache attribute updates.
 *
 *
 * ===============================================================
 *                     MENTAL MODEL
 * ===============================================================
 *
 *              BIOS knows NUMA topology
 *                      │
 *                      ▼
 *              ACPI SRAT describes it
 *                      │
 *                      ▼
 *              srat.c parses SRAT
 *                      │
 *                      ▼
 *          Builds CPU ↔ Node mappings
 *          Builds Memory ↔ Node mappings
 *          Builds NUMA distance matrix
 *                      │
 *                      ▼
 *             numa.c initializes pgdat,
 *             bootmem and zones
 *                      │
 *                      ▼
 *            Linux NUMA memory allocator works.
 *
 * ---------------------------------------------------------------
 * One-line summary:
 *
 * srat.c is the firmware-to-kernel bridge that translates ACPI SRAT
 * and SLIT tables into Linux NUMA nodes, CPU mappings, memory ranges,
 * and distance information before the VM subsystem is initialized.
 * ---------------------------------------------------------------
 */
```

