# High-Level DMA Path (Very Small Version)
+---------+     +--------+     +-----------+     +----------+
|  User   | --> |  VFS   | --> | Block I/O | --> | IDE Core |
+---------+     +--------+     +-----------+     +----------+
                                                        |
                                                        v
                                              +------------------+
                                              | ide_dma_setup()  |
                                              +------------------+
                                                        |
                                                        v
                                              +------------------+
                                              | ide_dma_start()  |
                                              +------------------+
                                                        |
                                                        v
                                          +-----------------------------+
                                          |   Bus Master IDE hardware   |
                                          +-----------------------------+
                                                        |
                                                        v
                                              (Interrupt from drive)
                                                        |
                                                        v
                                           +-------------------------+
                                           | ide_dma_intr() handler |
                                           +-------------------------+
                                                        |
                                                        v
                                          +------------------------------+
                                          | ide_end_request() finishes  |
                                          +------------------------------+

# Small Diagram: How PRD Table Is Used
DMA Setup:
  ide_build_dmatable() → fills dmatable_cpu[]

PRD Entry (8 bytes):
+-----------+------------+
| 32bit addr| 16 bit len |
+-----------+------------+
| reserved  |  EOT bit   |
+-----------+------------+

BMIDE reads PRD table:
  while(!EOT):
      DMA copy memory <-> disk

# Small Diagram: ATA Taskfile Write (DMA commands)
Before DMA starts, CPU writes:

OUTB(select,  IDE_SELECT_REG)
OUTB(count,   IDE_NSECTOR_REG)
OUTB(LBA_lo,  IDE_SECTOR_REG)
OUTB(LBA_mid, IDE_LCYL_REG)
OUTB(LBA_hi,  IDE_HCYL_REG)
OUTB(features,IDE_FEATURE_REG)

OUTB(ATA_CMD_READ_DMA or ATA_CMD_WRITE_DMA, IDE_COMMAND_REG)

CPU → IDE taskfile registers → Drive prepares DMA

# Small Diagram: Bus-Master IDE Registers
I/O base = hwif->dma_base

Offset  Register
0x00    dma_command
0x02    dma_status
0x04    dma_prdtable

dma_command bits:
  bit0 = START
  bit3 = R/W direction (1=read)

dma_status bits:
  bit0 = DMA active
  bit1 = ERROR
  bit2 = INTR

# Small Diagram: dma_setup() Flow
ide_dma_setup()
   |
   +--> build SG list
   |
   +--> pci_map_sg()
   |
   +--> fill PRD table
   |
   +--> OUTL(prd_dma, dma_prdtable)
   |
   +--> OUTB(direction, dma_command)

# Small Diagram: dma_start()
ide_dma_start()
   dma_cmd = INB(dma_command)
   OUTB(dma_cmd | 1, dma_command)   ← set START bit (bit0)
   drive->waiting_for_dma = 1

Result:
  Hardware begins DMA transfer

# Small Diagram: What The Hardware Does
Bus Master IDE Hardware:

Loop:
   read PRD entry
   for len bytes:
       PCI → Memory (READ DMA)
       or
       Memory → PCI (WRITE DMA)

After all PRDs:
   set dma_status.INTR
   raise IRQ to CPU

# Small Diagram: Interrupt Path
IRQ_n
  └─> ide_intr()
         └─> hwgroup->handler
                 └─> ide_dma_intr()

ide_dma_intr():
   dma_stat = hwif->ide_dma_end()
   stat = INB(IDE_STATUS_REG)
   pci_unmap_sg()
   ide_end_request()

# Small Diagram: ide_dma_end()
ide_dma_end()
   1) Stop DMA engine:
        cmd = INB(dma_command)
        OUTB(cmd & ~1, dma_command)

   2) Read status:
        stat = INB(dma_status)

   3) Clear INTR + ERROR bits:
        OUTB(stat | 0x6, dma_status)

   4) return stat

# Small Diagram: Full DMA State Machine
STATE 0: Request arrives
STATE 1: ide_dma_setup()
STATE 2: ATA taskfile programmed
STATE 3: ide_dma_start() → hardware active
STATE 4: DMA engine moves data
STATE 5: Hardware triggers IRQ
STATE 6: ide_dma_intr()
STATE 7: ide_end_request()
STATE 8: Next request

            +---------+
            |  S0 Req |
            +---------+
                 |
                 v
            +-----------+
            | S1 Setup  |
            +-----------+
                 |
                 v
            +-----------+
            | S2 TF Out |
            +-----------+
                 |
                 v
            +-----------+
            | S3 Start  |
            +-----------+
                 |
                 v
            +-----------+
            | S4 Active |
            +-----------+
                 |
                 v
            +-----------+
            | S5 IRQ    |
            +-----------+
                 |
                 v
            +-----------+
            | S6 intr() |
            +-----------+
                 |
                 v
            +-----------+
            | S7 EndReq |
            +-----------+
                 |
                 v
            +-----------+
            | S8 Next   |
            +-----------+


request_queue_t → request → ide_drive_t → ide_hwif_t → hw_regs_t → PRD table
                                        ↘ ide_hwgroup_t (interrupt context)
# data structures:
+-------------------------------------------------------------+
|  struct request_queue                                       |
+-------------------------------------------------------------+
                    |
                    v
+-------------------------------------------------------------+
|  struct request (rq)                                        |
+-------------------------------------------------------------+
                    |
                    v
+-------------------------------------------------------------+
|  struct ide_drive_t (drive)                                 |
+-------------------------------------------------------------+
| using_dma               waiting_for_dma                     |
| id (identify)           capacity                            |
| hwif ------------------------------+                        |
+-------------------------------------------------------------+
                                   v
+-------------------------------------------------------------+
|  struct ide_hwif_t (hwif)                                   |
+-------------------------------------------------------------+
| io_ports[]   irq   dma_base   dma_status   dma_command      |
| sg_table[]   dmatable_cpu     dmatable_dma                  |
| drives[2]                                               <---+
+-------------------------------------------------------------+
                    |
                    v
+-------------------------------------------------------------+
|  struct hw_regs_t (port mappings + irq + chipset)           |
+-------------------------------------------------------------+

INTERRUPT SIDE:
drive → HWIF → HWGROUP → ide_dma_intr()


# DMA read
request_queue → request → ide_drive_t → ide_hwif_t → PRD Table → BMIDE engine → IRQ → end_request


(1) Block layer submits READ request
------------------------------------
bio_submit()
   ↓
generic_make_request()
   ↓
elv_queue_request()
   ↓
do_ide_request(q)
   ↓
ide_do_request(drive, rq)

(2) IDE layer selects READ DMA path
------------------------------------
drive->using_dma == 1 ?
   → yes → ide_dma_setup(drive)

(3) SG → PRD translation
--------------------------
ide_map_sg(drive, rq)
pci_map_sg(...)
for each SG entry:
    PRD[i].addr  = sg_dma_address
    PRD[i].count = sg_dma_len
last_PRD |= EOT (End Of Table)

PRD CPU : hwif->dmatable_cpu[]
PRD DMA : hwif->dmatable_dma   ← given to hardware

(4) Program BMIDE PRD pointer register
---------------------------------------
OUTL(hwif->dmatable_dma, hwif->dma_prdtable)

(5) Setup DMA direction = READ
-------------------------------
dma_cmd = INB(hwif->dma_command)
dma_cmd &= ~IDE_DMA_WRITE   ← READ direction
OUTB(dma_cmd, hwif->dma_command)

(6) Program ATA Taskfile registers
-----------------------------------
OUTB(sector_count, IDE_NSECTOR_REG)
OUTB(lba_low,      IDE_SECTOR_REG)
OUTB(lba_mid,      IDE_LCYL_REG)
OUTB(lba_high,     IDE_HCYL_REG)
OUTB(device/head,  IDE_SELECT_REG)

Command issued:
OUTB(ATA_CMD_READ_DMA, IDE_COMMAND_REG)

(7) Start DMA engine
---------------------
dma_cmd = INB(hwif->dma_command)
dma_cmd |= IDE_DMA_START
OUTB(dma_cmd, hwif->dma_command)

Hardware starts:
    DISK → PCI → RAM via PRD table

(8) Disk finishes → triggers IRQ
--------------------------------
IRQ → ide_intr()
        ↓
     hwgroup->handler (ide_dma_intr)

(9) ide_dma_intr END PHASE
---------------------------
Stop DMA:
    dma_cmd = INB(dma_command)
    dma_cmd &= ~IDE_DMA_START
    OUTB(dma_cmd)

Check ATA STATUS:
    ERR? DF? DRQ?

pci_unmap_sg()
ide_end_request()




          +-------------+
          |  READ req   |
          +------+------+
                 |
                 v
        +--------+--------+
        | ide_dma_setup() |
        +--------+--------+
                 |
            Build PRD
                 |
                 v
        +--------+--------+
        | BMIDE PRD ptr   |
        +--------+--------+
                 |
      direction = READ (0)
                 |
                 v
        +--------+--------+
        | ATA READ DMA    |
        +--------+--------+
                 |
         Start DMA bit
                 |
                 v
   disk ---> memory (DMA engine)
                 |
                 v
            IRQ fired
                 |
        +--------+--------+
        | ide_dma_intr()  |
        +--------+--------+
                 |
            end_request


# DMA write
request_queue → request → ide_drive_t → ide_hwif_t → PRD Table → BMIDE engine → IRQ → end_request

(1) Block layer submits WRITE request
-------------------------------------
generic_make_request()
   ↓
do_ide_request()
   ↓
drive->using_dma ? yes

(2) ide_dma_setup(drive) called
-------------------------------
Build SG list
Build PRD table
program PRD DMA address

(3) Setup DMA direction = WRITE
-------------------------------
dma_cmd = INB(hwif->dma_command)
dma_cmd |= IDE_DMA_WRITE   ← WRITE direction SET
OUTB(dma_cmd)

(4) Program ATA taskfile for WRITE
-----------------------------------
OUTB(sector_count, IDE_NSECTOR_REG)
OUTB(lba_low,      IDE_SECTOR_REG)
OUTB(lba_mid,      IDE_LCYL_REG)
OUTB(lba_high,     IDE_HCYL_REG)
OUTB(device/head,  IDE_SELECT_REG)

Command:
OUTB(ATA_CMD_WRITE_DMA, IDE_COMMAND_REG)

(5) Start DMA
--------------
dma_cmd |= IDE_DMA_START
OUTB(dma_cmd)

Hardware performs:
    memory → disk

(6) Completion interrupt
-------------------------
ide_intr()
  → hwgroup->handler = ide_dma_intr()
     ↓
   ide_dma_intr()

(7) End-of-DMA work
--------------------
Stop DMA engine
Check status
pci_unmap_sg()
ide_end_request()


          +-------------+
          | WRITE req   |
          +------+------+
                 |
                 v
        +--------+--------+
        | ide_dma_setup() |
        +--------+--------+
                 |
            Build PRD
                 |
                 v
        +--------+--------+
        | BMIDE PRD ptr   |
        +--------+--------+
                 |
      direction = WRITE (1)
                 |
                 v
        +--------+--------+
        | ATA WRITE DMA   |
        +--------+--------+
                 |
         Start DMA bit
                 |
                 v
   memory ---> disk (DMA engine)
                 |
                 v
            IRQ fired
                 |
        +--------+--------+
        | ide_dma_intr()  |
        +--------+--------+
                 |
            end_request

# read & write differance
=================== DMA READ =======================
dma_cmd &= ~WRITE
ATA_CMD = READ_DMA
Disk → RAM
====================================================

=================== DMA WRITE ======================
dma_cmd |= WRITE
ATA_CMD = WRITE_DMA
RAM → Disk
====================================================

# DMA address registeration
 +--------------------+       +------------------------+
 | Block Layer Pages  |       |  ide_dma_setup()       |
 +--------------------+       +-----------+------------+
         |                                  |
         | CPU addresses only               |
         |                                  |
         v                                  v
+-------------------+            +----------------------+
| sg_table entries  | ←─── ide_map_sg()  CPU addresses |
+---------+---------+            +----------+-----------+
          |                                   |
          |                                   |  DMA ADDRESS DECIDED HERE
          |                                   v
          |                     +------------------------------+
          |                     | pci_map_sg()                 |
          |                     |  returns DMA BUS addresses   |
          v                     +---------------+--------------+
+-------------------+                           |
| sg_dma_address()  |  <-- now contains DMA ----+
| sg_dma_len()      |
+--------+----------+
         |
         v
+--------------------------+
| Build PRD table          |
|   PRD.addr = dma_addr    |
|   PRD.len  = dma_len     |
+------------+-------------+
             |
             v
+-----------------------------+
| HWIF->OUTL(PRD_table_addr) |
+-------------+---------------+
              |
              v
      +----------------+
      | BMIDE DMA chip|
      +----------------+

