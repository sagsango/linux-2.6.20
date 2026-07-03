```c
/*
 * FILE: arch/x86_64/pci/k8-bus.c
 *
 * PURPOSE:
 *   Discover PCI bus → NUMA node mapping on AMD K8 systems.
 *
 * BIG IDEA:
 *   Devices behind a PCI/HyperTransport bus are closer to one NUMA node.
 *   This code reads AMD K8 northbridge registers and marks each PCI bus
 *   with the node it belongs to.
 *
 * Final result:
 *
 *      pci_bus->sysdata = node_id
 */

/*
 * ============================================================
 * 1. BACKGROUND
 * ============================================================
 *
 * AMD K8 systems are NUMA machines.
 *
 * Each CPU/socket has:
 *
 *      CPU cores
 *      memory controller
 *      HyperTransport links
 *      PCI/IO buses behind those links
 *
 * So a PCI device is physically closer to one NUMA node.
 *
 * Example:
 *
 *      Node0
 *        |
 *        +-- HT Link
 *              |
 *              +-- PCI Bus 5
 *                    |
 *                    +-- NIC
 *
 * The NIC is closer to Node0.
 *
 * Linux wants to know this for:
 *
 *      DMA locality
 *      interrupt affinity
 *      device allocation policy
 *      NUMA-aware I/O placement
 */

/*
 * ============================================================
 * 2. HARDWARE REGISTERS
 * ============================================================
 */

#define NODE_ID_REGISTER 0x60

/*
 * K8 northbridge register containing node ID.
 */

#define NODE_ID(dword) (dword & 0x07)

/*
 * Extract low 3 bits.
 *
 * Node IDs:
 *
 *      0..7
 */

#define LDT_BUS_NUMBER_REGISTER_0 0x94
#define LDT_BUS_NUMBER_REGISTER_1 0xB4
#define LDT_BUS_NUMBER_REGISTER_2 0xD4

/*
 * LDT = Lightning Data Transport, old name for HyperTransport.
 *
 * K8 has multiple HT links.
 *
 * Each register describes PCI bus range behind one HT link.
 */

#define NR_LDT_BUS_NUMBER_REGISTERS 3

#define SECONDARY_LDT_BUS_NUMBER(dword) ((dword >> 8) & 0xFF)
#define SUBORDINATE_LDT_BUS_NUMBER(dword) ((dword >> 16) & 0xFF)

/*
 * PCI bridge-style bus numbering:
 *
 *      secondary bus:
 *          first bus behind this link
 *
 *      subordinate bus:
 *          last bus behind this link
 *
 * So bus range is:
 *
 *      secondary ... subordinate
 */

#define PCI_DEVICE_ID_K8HTCONFIG 0x1100

/*
 * AMD K8 HyperTransport config device.
 */

/*
 * ============================================================
 * 3. MAIN FUNCTION
 * ============================================================
 */

__init static int fill_mp_bus_to_cpumask(void)
{
        struct pci_dev *nb_dev = NULL;
        int i, j;
        u32 ldtbus, nid;

        static int lbnr[3] = {
                LDT_BUS_NUMBER_REGISTER_0,
                LDT_BUS_NUMBER_REGISTER_1,
                LDT_BUS_NUMBER_REGISTER_2
        };

        while ((nb_dev = pci_get_device(PCI_VENDOR_ID_AMD,
                                        PCI_DEVICE_ID_K8HTCONFIG,
                                        nb_dev))) {
                pci_read_config_dword(nb_dev, NODE_ID_REGISTER, &nid);

                for (i = 0; i < NR_LDT_BUS_NUMBER_REGISTERS; i++) {
                        pci_read_config_dword(nb_dev, lbnr[i], &ldtbus);

                        if (!(SECONDARY_LDT_BUS_NUMBER(ldtbus) == 0 &&
                              SUBORDINATE_LDT_BUS_NUMBER(ldtbus) == 0)) {
                                for (j = SECONDARY_LDT_BUS_NUMBER(ldtbus);
                                     j <= SUBORDINATE_LDT_BUS_NUMBER(ldtbus);
                                     j++) {
                                        struct pci_bus *bus;
                                        long node = NODE_ID(nid);

                                        bus = pci_find_bus(0, j);
                                        if (!bus)
                                                continue;

                                        if (!node_online(node))
                                                node = 0;

                                        bus->sysdata = (void *)node;
                                }
                        }
                }
        }

        return 0;
}

/*
 * ============================================================
 * 4. CODE WALK
 * ============================================================
 *
 *      struct pci_dev *nb_dev = NULL;
 *
 * Used to iterate over all AMD K8 HT config devices.
 *
 *
 *      pci_get_device(AMD, K8HTCONFIG, nb_dev)
 *
 * Finds each AMD K8 northbridge HT config PCI function.
 *
 * Each K8 node/socket can expose such a PCI config device.
 *
 *
 *      pci_read_config_dword(nb_dev, NODE_ID_REGISTER, &nid);
 *
 * Read hardware node ID.
 *
 *
 *      NODE_ID(nid)
 *
 * Extract node ID from low 3 bits.
 *
 *
 *      pci_read_config_dword(nb_dev, lbnr[i], &ldtbus);
 *
 * Read bus range behind each HyperTransport link.
 *
 *
 *      secondary = bits 15:8
 *      subordinate = bits 23:16
 *
 *
 * If both are zero:
 *
 *      no buses behind this HT link
 *
 *
 * Otherwise:
 *
 *      for every bus number in that range:
 *
 *          find pci_bus object
 *          assign bus->sysdata = node
 */

/*
 * ============================================================
 * 5. FLOW DIAGRAM
 * ============================================================
 *
 *      fill_mp_bus_to_cpumask()
 *          |
 *          v
 *      find AMD K8 HT config PCI device
 *          |
 *          v
 *      read NODE_ID_REGISTER
 *          |
 *          v
 *      node = nid & 0x7
 *          |
 *          v
 *      for each HT/LDT bus register:
 *          |
 *          v
 *      read secondary/subordinate bus range
 *          |
 *          +-- secondary=0 and subordinate=0
 *          |       |
 *          |       v
 *          |    no bus on this link
 *          |
 *          +-- valid range
 *                  |
 *                  v
 *              for bus j in range:
 *                  |
 *                  v
 *              pci_find_bus(0, j)
 *                  |
 *                  +-- not found: skip
 *                  |
 *                  v
 *              if node offline:
 *                  node = 0
 *                  |
 *                  v
 *              bus->sysdata = node
 */

/*
 * ============================================================
 * 6. EXAMPLE
 * ============================================================
 *
 * Suppose K8 node register says:
 *
 *      NODE_ID = 1
 *
 * LDT register says:
 *
 *      secondary bus    = 8
 *      subordinate bus  = 12
 *
 * Then this code does:
 *
 *      bus 8  -> node 1
 *      bus 9  -> node 1
 *      bus 10 -> node 1
 *      bus 11 -> node 1
 *      bus 12 -> node 1
 *
 * Result:
 *
 *      pci_bus(8)->sysdata = 1
 *      pci_bus(9)->sysdata = 1
 *      ...
 */

/*
 * ============================================================
 * 7. WHY bus->sysdata?
 * ============================================================
 *
 * struct pci_bus has architecture-specific private data:
 *
 *      void *sysdata;
 *
 * This code stores the NUMA node there.
 *
 * Later PCI code can ask:
 *
 *      which NUMA node is this PCI bus close to?
 *
 * and recover node from:
 *
 *      bus->sysdata
 */

/*
 * ============================================================
 * 8. IMPORTANT LIMITATION
 * ============================================================
 *
 * Comment says:
 *
 *      This assumes HT node IDs == Linux node IDs.
 *
 * But that is not always guaranteed.
 *
 * Hardware node ID:
 *
 *      K8 / HyperTransport node number
 *
 * Linux node ID:
 *
 *      kernel's internal NUMA node number
 *
 * They are often the same on simple systems, but not always.
 *
 * So this is a historical/simple implementation.
 */

/*
 * ============================================================
 * 9. WHY fs_initcall?
 * ============================================================
 */

fs_initcall(fill_mp_bus_to_cpumask);

/*
 * This function runs during kernel initcall phase.
 *
 * fs_initcall is relatively late compared to early boot.
 *
 * By then:
 *
 *      PCI subsystem is initialized enough
 *      pci_bus objects exist
 *      NUMA nodes are known
 *
 * So it can attach node IDs to PCI buses.
 */

/*
 * ============================================================
 * 10. FINAL MENTAL MODEL
 * ============================================================
 *
 * This file answers:
 *
 *      "Which NUMA node is this PCI bus close to?"
 *
 * It does that by reading AMD K8 HyperTransport bus-number registers.
 *
 * Flow:
 *
 *      K8 northbridge
 *          |
 *          v
 *      node ID + HT bus ranges
 *          |
 *          v
 *      PCI bus number
 *          |
 *          v
 *      pci_bus->sysdata = node
 *
 *
 * One-line summary:
 *
 *      k8-bus.c maps PCI buses to NUMA nodes on AMD K8 systems by reading
 *      HyperTransport bus range registers from the K8 northbridge.
 */
```

