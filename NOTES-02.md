# ide_hwgroup = a group of 1 or more hwifs that SHARE a single IRQ
Primary channel (ide0)  → IRQ 14
Secondary channel (ide1)→ IRQ 14  (shared)

                +-----------------------------+
IRQ 14  ------> |        ide_hwgroup_t        |
                +-----------------------------+
                | handler()                  |
                | timer                      |
                | current drive              |
                | current request            |
                +-------------+---------------+
                              |
         +--------------------+---------------------+
         |                                          |
+--------------------+                     +--------------------+
|     ide_hwif_t     |                     |     ide_hwif_t     |
|       ide0         |                     |       ide1         |
+--------------------+                     +--------------------+
| uses shared irq    |                     | uses shared irq    |
+--------------------+                     +--------------------+


# data structures
                   +------------------------------+
                   |        ide_hwgroup_t         |
                   |   (shared IRQ, timer, work)  |
                   +------------------------------+
                         ^                ^
                         |                |
         +----------------+                +----------------+
         |                                                   |
+---------------------------+                   +----------------------------+
|        ide_hwif_t         |                   |        ide_hwif_t          |
|        Channel 0          |                   |        Channel 1           |
+---------------------------+                   +----------------------------+
| io ports, dma regs        |                   | io ports, dma regs         |
| DMA ops, PIO ops          |                   | DMA ops, PIO ops           |
| drives[0], drives[1]      |                   | drives[0], drives[1]       |
+---------------+-----------+                   +---------------+------------+
                |                                               |
                v                                               v
       +--------------------+                         +--------------------+
       |   ide_drive_t      |                         |   ide_drive_t      |
       |       hda          |                         |       hdc          |
       +--------------------+                         +--------------------+
       |   ide_drive_t      |                         |   ide_drive_t      |
       |       hdb          |                         |       hdd          |
       +--------------------+                         +--------------------+




# PCI IDE CONTROLLER + BAR REGISTERS
                  +================================================+
                  |        PCI IDE/PATA CONTROLLER CHIPSET         |
                  +================================================+
                  |  PCI BAR0/1 → Command Block Registers (CH0)    |
                  |  PCI BAR2/3 → Command Block Registers (CH1)    |
                  |  PCI BAR4   → Bus-Master DMA Registers         |
                  +================================================+
                                   |               |
                         ----------+               +----------
                         |                                |
                         v                                v
                 +-----------------+             +-----------------+
                 |  IDE Channel 0  |             |  IDE Channel 1  |
                 |   (Primary)     |             |   (Secondary)   |
                 +-----------------+             +-----------------+


# ide_hwif_t — the software model of one IDE channel 
                      +-----------------------------------------+
                      |          struct ide_hwif_t (hwif)       |
                      +-----------------------------------------+
                      | name: "ide0" or "ide1"                  |
                      | index: 0 or 1                           |
                      | channel: 0=primary, 1=secondary         |
                      |                                         |
                      | io_ports[]  ---> I/O port addresses     |
                      | dma_base    ---> BMIDE base address     |
                      | dma_prdtable---> PRD table physical addr|
                      | irq         ---> shared IRQ (14,15,etc) |
                      |                                         |
                      | drives[0] master → ide_drive_t hda/hdc  |
                      | drives[1] slave  → ide_drive_t hdb/hdd  |
                      |                                         |
                      | PCI data (pci_dev*)                     |
                      | chipset tuning functions                |
                      | DMA function pointers:                  |
                      |   dma_setup(), dma_start(), dma_end()   |
                      |   ata_input_data()/output_data()        |
                      | PIO ops: OUTB, INB, OUTSW, INSW, ...    |
                      +-----------------------------------------+

# Two drives per hwif
# +---------------------------- ide_hwif_t (ide0) -----------------------------+
|                                                                           |
|  +-------------------+        +-------------------+                       |
|  | ide_drive_t       |        | ide_drive_t       |                       |
|  | Master (hda)      |        | Slave (hdb)       |                       |
|  | drive->select ... |        | drive->select ... |                       |
|  | drive->io_16bit   |        | drive->using_dma  |                       |
|  | ...               |        | ...               |                       |
|  +-------------------+        +-------------------+                       |
+---------------------------------------------------------------------------+


# All hwifs sharing the same IRQ go into ide_hwgroup_t
                      (Shared IRQ Path)
                         IRQ 14
                           |
                           v
                +================================+
                |        struct ide_hwgroup_t    |
                +================================+
                | handler()      - active ISR    |
                | handler_save() - saved ISR     |
                | busy           - group locked  |
                | polling        - for ATAPI     |
                | resetting      - for resets    |
                | pci_dev        - controller    |
                | drive          - current drive |
                | rq             - current req   |
                | timer          - timeout timer |
                | expiry()       - timeout call  |
                +================================+
                           ^
                           |
           +---------------+---------------------------+
           |                                               |
+--------------------------+                  +--------------------------+
|         ide_hwif_t       |                  |         ide_hwif_t       |
|         ide0             |                  |         ide1             |
+--------------------------+                  +--------------------------+
| irq = 14                 |                  | irq = 14                 |
| hwgroup = same group     |                  | hwgroup = same group     |
+--------------------------+                  +--------------------------+


# WITHIN ide_hwif_t: DMA STRUCTURE
+-------------------- ide_hwif_t ------------------------+
|                                                        |
| dma_prdtable (physical address) --------------------+  |
| sg_table (scatter-gather list)                      |  |
|                                                     |  |
| PRD Table layout (used by BMIDE):                   |  |
|   +-----------+-----------+                         |  |
|   | addr0     | length0   |---+                     |  |
|   | addr1     | length1   |   | DMA engine reads    |  |
|   | ...       | ...       |   | PRD entries         |  |
|   +-----------+-----------+<--+                     |  |
|                                                        |
+--------------------------------------------------------+


# COMPLETE DATA FLOW: ext2 → block layer → IDE → DMA → IRQ → finish
 USER READ/WRITE
        |
        v
  ext2_file_read / ext2_get_block
        |
        v
  submit_bio()
        |
        v
  generic_make_request()
        |
        v
  __make_request() → elevator (CFQ/deadline)
        |
        v
  request_queue->request_fn  (IDE path)
        |
        v
  do_ide_request()      <---- in drivers/ide/ide.c
        |
        |--- if drive->using_dma:
        |        dma_setup()
        |        build PRD table
        |        dma_start()
        |        (controller starts DMA engine)
        |
        +--- else:
                 PIO mode

# DMA Engine + IRQ Handling 
 DMA engine running (hardware)
        |
        v
+------------------------------------------------------+
|  PCI IDE Bus-Master DMA                              |
|  Transfer disk <-> memory using PRD entries          |
+------------------------------------------------------+
        |
        v
DMA DONE → raises IRQ (14 or 15)
        |
        v
+-----------------------+
| Linux IRQ handler     |
+-----------------------+
        |
        v
hwgroup->handler()   (often ide_dma_intr)
        |
        v
ide_dma_end()
        |
        v
ide_end_request()
        |
        v
request completes
(ext2 continues)


# GLOBAL VIEW — EVERYTHING TOGETHER
                         +==========================+
                         |   PCI IDE CONTROLLER     |
                         +==========================+
                         | BAR0/BAR1: IDE0 regs     |
                         | BAR2/BAR3: IDE1 regs     |
                         | BAR4: DMA registers      |
                         +============+=============+
                                      |
                    +----------------+----------------+
                    |                                 |
                    v                                 v
           +------------------+              +------------------+
           | ide_hwif_t ide0  |              | ide_hwif_t ide1  |
           +------------------+              +------------------+
           | io_ports[]       |              | io_ports[]       |
           | dma_base         |              | dma_base         |
           | drives[0], [1]   |              | drives[0], [1]   |
           | dma_setup()      |              | dma_setup()      |
           | dma_start()      |              | dma_start()      |
           | dma_end()        |              | dma_end()        |
           +---------+--------+              +---------+--------+
                     |                                 |
      +--------------+                                 +--------------+
      |                                                                 |
      v                                                                 v
+----------------+                                         +----------------+
|  ide_drive_t   |                                         |  ide_drive_t   |
|    (hda)       |                                         |    (hdc)       |
+----------------+                                         +----------------+
|  using_dma=1   |                                         |  using_dma=1   |
+----------------+                                         +----------------+

               Both channels share the same IRQ
                     (example IRQ 14)
                             |
                             v
                    +---------------------+
                    |  ide_hwgroup_t      |
                    | - active handler    |
                    | - timer             |
                    | - current drive     |
                    | - current request   |
                    +---------------------+




