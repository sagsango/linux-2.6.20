# HIGH-LEVEL ASCII DIAGRAM — ENTIRE DMA SYSTEM
 USER SPACE
    |
    v
 VFS → ext2 → submit_bio
    |
    v
BLOCK LAYER
    |
    v
+--------------------------+
|  generic_make_request()  |
+--------------------------+
    |
    v
ELEVATOR / QUEUE
    |
    v
request_fn() → ide_do_request()
    |
    v
+--------------------------+
|   ide_dma_setup()        |
|   ide_build_dmatable()   |
|   ide_dma_exec_cmd()     |
|   ide_dma_start()        |
+--------------------------+
    |
    v
PCI IDE DMA ENGINE  <-- hardware moves data using PRD table
    |
    v
IDE DMA INTERRUPT
    |
    v
+---------------------------+
|        ide_dma_intr()     |
|        ide_dma_end()      |
|        ide_end_request()  |
+---------------------------+
    |
    v
BLOCK LAYER completion


# CONCEPTUAL COMPONENTS
+---------------------------+
|  ide_hwif_t (IDE Channel) |
+---------------------------+
| - io ports (taskfile)     |
| - DMA registers           |
| - PRD table CPU pointer   |
| - PRD table DMA pointer   |
| - scatterlist             |
| - DMA callbacks           |
+---------------------------+

+---------------------------+
|  ide_drive_t (Drive)      |
+---------------------------+
| - using_dma flag          |
| - drive ID (hd_driveid)   |
| - DMA capable?            |
| - waiting_for_dma flag    |
+---------------------------+

+---------------------------+
| ide_hwgroup_t (IRQ Group) |
+---------------------------+
| - shared IRQ handler      |
| - current request         |
| - timer / expiry handler  |
+---------------------------+


# Drive Whitelist / Blacklist
Used to decide whether a drive is “trusted” for DMA.

drive_whitelist[] → drives KNOWN safe for DMA
drive_blacklist[] → drives KNOWN unsafe


Used in:
__ide_dma_good_drive()
__ide_dma_bad_drive()
config_drive_for_dma()

+-------------+        +-------------+
| whitelist[] | <----> | drive model |
+-------------+        +-------------+
| blacklist[] |
+-------------+


# SG Mapping: ide_build_sglist()
rq → scatterlist → pci_map_sg() → DMA addresses


REQUEST
  |
  | (bio)
  v
+----------------------------+
| CPU virtual addresses      |
+----------------------------+
       |
       | pci_map_sg()
       v
+----------------------------+
|  DMA bus addresses (sg)    |
+----------------------------+


# PRD Building: ide_build_dmatable()
The IDE DMA engine requires a PRD (Physical Region Descriptor) table:
Each PRD entry:
+---------------------+------------------+
| Buffer DMA Address | Length (max 64K) |
+---------------------+------------------+

ASCII representation:
PRD TABLE
---------
Entry 0 → [addr0][len0]
Entry 1 → [addr1][len1]
Entry 2 → [addr2][len2]
...
Last Entry → set end-of-table bit (0x80000000)

flow:
SG list
   |
   v
Split each sg segment into <= 64KB chunks
   |
   v
Write PRD entries into dma table

# ide_dma_setup() — BEGIN DMA PHASE
ide_dma_setup()
  |
  |--> build PRD table
  |--> write PRD table address to controller
  |--> set read/write flag
  |--> clear DMA status bits (error/interrupt)
  |--> drive->waiting_for_dma = 1

Registers written:
BMIDE:
  dma_prdtable  ← PRD table physical address
  dma_command   ← R/W direction
  dma_status    ← clear old IRQ + ERR bits


CPU                              PCI IDE BM-DMA
-----------------------------------------------
OUTL PRD ptr -----------> dma_prdtable_reg
OUTB R/W bit -----------> dma_command_reg
OUTB clr status --------> dma_status_reg


# ide_dma_exec_cmd() — Send ATA command
Issues an ATA command (READ/WRITE DMA) and sets the interrupt handler:
ATA command → ide_execute_command(command, &ide_dma_intr)

       PIO write of ATA command
             |
             v
     +---------------------+
     |   Drive Taskfile    |
     +---------------------+
             |
             v
   hardware prepares DMA transfer

# ide_dma_start() — START DMA ENGINE
dma_command |= 1
OUTB(dma_command, dma_command_reg)

+--------------------------------------------+
| Set BMIDE "start" bit → hardware starts DMA |
+--------------------------------------------+

# Hardware phase (no cpu)
PCI IDE DMA ENGINE reads PRD table
    |
    +--> bus-master transfer memory <→ drive

+---------------------------+
|   PCI IDE BM-DMA Engine   |
+---------------------------+
| Reads PRD table           |
| Performs DMA transfer     |
| Raises IRQ on completion  |
+---------------------------+

# ide_dma_intr() — DMA Interrupt Handler

ide_dma_intr()
  |
  |--> call ide_dma_end() through HWIF->ide_dma_end
  |--> read drive status (taskfile)
  |--> if OK, complete request
  |--> else call ide_error()

IRQ occurs
   |
   v
+-------------------+
| ide_dma_intr()    |
+-------------------+
   |
   |---> ide_dma_end()
   |
   |---> read IDE_STATUS_REG
   |
   |---> if all OK:
            request->end_request()
        else:
            ide_error()

# ide_dma_end() — Stop DMA, cleanup
stop DMA → OUTB(dma_command & ~1)
read dma_status
clear intr + error flags
unmap sg tables
return success/error

DMA engine
   |
   | stop bit cleared
   v
+-------------------------------------+
| HW stops DMA, status bits captured |
+-------------------------------------+
   |
   | pci_unmap_sg()
   v
Block Layer ← “completed/done/error”


# Timeout Handling: dma_timer_expiry()
dma_timer_expiry()
   |
   |--> read dma_status
   |--> if error → retry in PIO
   |--> if still DMA ACTIVE → WAIT_CMD
   |--> if interrupt flag → allow IRQ to finish

No IRQ?
   |
   v
+---------------------------+
|   dma_timer_expiry()     |
+---------------------------+
| check busy/error flags    |
| maybe PIO fallback        |
| maybe retry               |
+---------------------------+

# DMA ON/OFF helpers
__ide_dma_on()
__ide_dma_off()
__ide_dma_host_on()
__ide_dma_host_off()

dma_status register (bit 5/6)

bit 5 → master
bit 6 → slave

hwif->dma_status:
 5: DMA for master
 6: DMA for slave


# ide_setup_dma() — INITIALIZATION
- allocate PRD table (pci_alloc_consistent)
- reserve I/O ports if needed
- assign dma_command/dma_status/dma_prdtable offsets
- install dma function pointers

ide_setup_dma()
    |
    | allocate PRD table (consistent DMA memory)
    | set dma_base = PCI BAR4
    | set dma_command/status/prdtable registers
    |
    v
 hwif->dma_* filled
 hwif->ide_dma_* pointers set


# full pipeline
USER READ
    |
    v
ext2 / block layer
    |
    v
+------------------------------+
| submit_bio()                 |
+------------------------------+
    |
    v
+------------------------------+
| generic_make_request()       |
+------------------------------+
    |
    v
+------------------------------+
| elevator / queue (CFQ etc.)  |
+------------------------------+
    |
    v
+------------------------------+
| request_fn → ide_do_request |
+------------------------------+
    |
    |-- if using_dma:
    v
+------------------------------+
|   ide_dma_setup()           |
+------------------------------+
    |
    v
+------------------------------+
|  ide_build_sglist()         |
|  pci_map_sg()               |
|  ide_build_dmatable()       |
+------------------------------+
    |
    v
+------------------------------+
|  PRD table in DMA memory     |
+------------------------------+
    |
    v
+------------------------------+
| ide_dma_exec_cmd()          |
|   (send ATA DMA command)    |
+------------------------------+
    |
    v
+------------------------------+
| ide_dma_start()             |
|   (start BMIDE engine)      |
+------------------------------+
    |
    v
+------------------------------+
| HARDWARE DMA TRANSFER       |
| Drive <--> Memory via PRD   |
+------------------------------+
    |
    v
+------------------------------+
|   IRQ → ide_dma_intr()      |
+------------------------------+
    |
    v
+------------------------------+
| ide_dma_end()               |
+------------------------------+
    |
    v
+------------------------------+
| ide_end_request()           |
+------------------------------+
    |
    v
BLOCK COMPLETE

# gaint flow 
                 ┌──────────────────────────────────────────────────────────────────┐
                 │          User does read/write to /dev/hda                        │
                 └──────────────────────────────────────────────────────────────────┘
                                   |
                                   v
            ┌──────────────────────────────────────────────┐
            │ generic_make_request(struct bio *bio)         │
            └──────────────────────────────────────────────┘
                                   |
                                   v
            ┌──────────────────────────────────────────────┐
            │ bio mapped into request (struct request)      │
            │ elevator schedules request                    │
            └──────────────────────────────────────────────┘
                                   |
                                   v
          ┌──────────────────────────────────────────────────────────────┐
          │ do_ide_request()  (IDE request_fn registered with blk layer) │
          └──────────────────────────────────────────────────────────────┘
                                   |
                                   v
          ┌──────────────────────────────────────────────────────────────┐
          │ HWGROUP(drive) is locked                                     │
          │ request selected from drive->queue                           │
          │ drive->rq = active request                                   │
          └──────────────────────────────────────────────────────────────┘
                                   |
                                   v
    ┌────────────────────────────────────────────────────────────────────────────────┐
    │ CHECK: Can this drive use DMA? (drive->using_dma + ide_use_dma())              │
    │ If not: fallback to PIO                                                        │
    └────────────────────────────────────────────────────────────────────────────────┘
                                   |
                                   v
    ┌───────────────────────────────────────────────────────────────┐
    │ ide_dma_setup(drive)                                          │
    │   → build PRD table                                           │
    │   → build scatter-gather list                                 │
    │   → pci_map_sg() maps pages for DMA                           │
    │   → OUTL(hwif->dmatable_dma, hwif->dma_prdtable)              │
    │     (program PRD base register)                                │
    └───────────────────────────────────────────────────────────────┘
                                   |
                                   v
    ┌─────────────────────────────────────────────────────────────────────────────┐
    │              ATA TASKFILE REGISTER PROGRAMMING (PIO writes!)               │
    │-----------------------------------------------------------------------------│
    │ OUTB(drive->select,      IDE_SELECT_REG)                                    │
    │ OUTB(nsectors,            IDE_NSECTOR_REG)                                  │
    │ OUTB(LBA low,             IDE_SECTOR_REG)                                   │
    │ OUTB(LBA mid,             IDE_LCYL_REG)                                     │
    │ OUTB(LBA high,            IDE_HCYL_REG)                                     │
    │ OUTB(features,            IDE_FEATURE_REG)                                  │
    │ OUTB(ATA_CMD_READ_DMA or ATA_CMD_WRITE_DMA, IDE_COMMAND_REG)               │
    └─────────────────────────────────────────────────────────────────────────────┘
                                   |
                                   v
    ┌───────────────────────────────────────────────────────────────┐
    │ ide_dma_start():                                              │
    │    dma_cmd = INB(hwif->dma_command)                           │
    │    OUTB(dma_cmd | 1, hwif->dma_command)  → start DMA engine   │
    │                                                               │
    │  This sets BMIDE "start" bit (bit0)                           │
    └───────────────────────────────────────────────────────────────┘
                                   |
                                   v
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                           BUS MASTER IDE DMA ENGINE (HARDWARE)                            │
│------------------------------------------------------------------------------------------│
│ Reads PRD table from (hwif->dmatable_dma)                                                │
│ For each PRD entry:                                                                      │
│     addr = PRD.addr                                                                       │
│     count = PRD.count                                                                     │
│     direction = read/write according to dma_command bit3                                  │
│                                                                                          │
│ Reads/writes memory using PCI DMA                                                        │
│                                                                                          │
│ On completion of DMA or error:                                                           │
│     → Set dma_status bits:                                                               │
│         bit0 = DMA active                                                                │
│         bit1 = ERROR                                                                      │
│         bit2 = INTR                                                                      │
│                                                                                          │
│     → Raise IRQ on system's PIC/APIC                                                     │
└──────────────────────────────────────────────────────────────────────────────────────────┘
                                   |
                                   |
                                   v
                    ┌───────────────────────────────────────┐
                    │      Linux IRQ LINE FOR IDE            │
                    │ ex: IRQ14 for primary, IRQ15 secondary │
                    └───────────────────────────────────────┘
                                   |
                                   v
           ┌────────────────────────────────────────────────────────────┐
           │ ide_intr() – global IDE interrupt service routine          │
           └────────────────────────────────────────────────────────────┘
                                   |
                                   v
      ┌───────────────────────────────────────────────────────────────────────┐
      │ HWGROUP(drive)->handler = ide_dma_intr                               │
      │   → calls drive-specific handler                                      │
      │   → dma_intr reads:                                                   │
      │       dma_stat = hwif->ide_dma_end()                                  │
      │       stat     = INB(IDE_STATUS_REG)                                  │
      │   → clears dma_status|6 (clear INTR and ERR bits)                     │
      │   → unmap DMA memory (pci_unmap_sg())                                 │
      └───────────────────────────────────────────────────────────────────────┘
                                   |
                                   v
           ┌────────────────────────────────────────────────────────────┐
           │ ide_end_request(): update request, free buffers            │
           │ if all sectors done → complete request to block layer      │
           └────────────────────────────────────────────────────────────┘
                                   |
                                   v
                       ┌─────────────────────────────────┐
                       │ Block layer wakes waiting tasks │
                       └─────────────────────────────────┘

# Data structres
# ide_hwif_t (Host Interface — DMA related only)
ide_hwif_t
├─ io_ports[IDE_NR_PORTS]          → ATA taskfile PIO register addresses
├─ dma_base                        → BMIDE base address (I/O mapped)
├─ dma_command                     → BMIDE command register (offset 0)
├─ dma_status                      → BMIDE status (offset 2)
├─ dma_prdtable                    → PRD table pointer register (offset 4)
├─ dmatable_cpu                    → CPU pointer to PRD table
├─ dmatable_dma                    → DMA physical address for PRD table
├─ sg_table                        → scatterlist array
├─ sg_nents                        → number of SG entries
├─ OUTB(), INB(), OUTL()           → low level port I/O
├─ dma_setup                       → function pointer
├─ dma_start
├─ ide_dma_end
├─ ide_dma_on
├─ ide_dma_off_quietly
├─ ide_dma_test_irq
├─ ide_dma_timeout
└─ pci_dev                         → needed for pci_map_sg()

# ide_drive_t (Drive State — DMA related) 
ide_drive_t
├─ using_dma              → 1 if DMA enabled
├─ waiting_for_dma        → 1 if DMA in progress
├─ media                  → type (disk/cd/etc)
├─ id                     → hd_driveid (DMA capability bits)
├─ rq                     → active struct request
├─ hwif                   → pointer to ide_hwif_t
├─ bad_wstat              → ignore write errors
├─ flags:
│   ├─ retry_pio
│   ├─ no_io_32bit
│   ├─ autodma
│   ├─ addressing
└─ name                   → "hda"

# ide_hwgroup_t (Shared channel state)
ide_hwgroup_t
├─ handler         → currently installed interrupt handler (e.g., ide_dma_intr)
├─ handler_save
├─ busy            → lock for channel
├─ drive           → pointer to current drive
├─ hwif            → the hwif currently running
├─ rq              → active request
├─ timer           → timeout for DMA (WAIT_CMD)
├─ poll_timeout
└─ expiry          → dma timer expiry callback

# REGISTER LEVEL DIAGRAMS
+----------------------+----------------------------+
| Register             | Meaning                    |
+----------------------+----------------------------+
| IDE_DATA_REG         | PIO data                   |
| IDE_ERROR_REG        | Error / Feature            |
| IDE_NSECTOR_REG      | Sector count               |
| IDE_SECTOR_REG       | LBA low                    |
| IDE_LCYL_REG         | LBA mid                    |
| IDE_HCYL_REG         | LBA high                   |
| IDE_SELECT_REG       | Select drive/head/LBA      |
| IDE_STATUS_REG       | Status                     |
| IDE_COMMAND_REG      | ATA command                |
+----------------------+----------------------------+

write commnds: OUTB(cmd, IDE_COMMAND_REG)

Bus Master IDE Registers:
dma_base + 0  → dma_command register
dma_base + 2  → dma_status register
dma_base + 4  → dma_prdtable register

dma_command register (bit layout)
bit0 = Start/Stop DMA
bit3 = Read (1) / Write (0)

dma_status register:
bit0 = DMA active
bit1 = ERROR
bit2 = INTR   (interrupt)

PRD Table Format (Physical Region Descriptor):
+-------------------------------+
| 0-3: Physical base address    |
| 4-5: Byte count               |
| 6-7: Reserved                 |
|     bit31 = End-of-table bit  |
+-------------------------------+

# CALL STACK DIAGRAMS
do_ide_request()
  → start request
     → ide_dma_setup()
         → ide_build_dmatable()
             → ide_build_sglist()
                 → ide_map_sg()
                     → pci_map_sg()
         → OUTL(prd_dma, dma_prdtable)
         → OUTB(read/write flag, dma_command)

ide_dma_start()
  → dma_cmd = INB(dma_command)
  → OUTB(dma_cmd | 1, dma_command)
  → DMA engine starts in hardware

hardware IRQ
  → ide_intr()
        → hwgroup->handler (usually ide_dma_intr)
             → ide_dma_intr()
                 → ide_dma_end()
                      → STOP dma_command & unmap PRD memory
                      → clear dma_status bits
                 → end_request()


# ERROR / FALLBACK PATH
dma_timer_expiry()
  → dma_status read
  → if ERROR bit set → -1 (will fallback)
  → if DMA still active → WAIT_CMD
  → else IRQ expected → WAIT_CMD

__ide_dma_lostirq()
  → print warning
  → return 1

__ide_dma_timeout()
  → read dma_status
  → if test_irq() says interrupt pending → OK
  → else → ide_dma_end() with error

if (ide_dma_setup returns 1)
    → fallback to PIO engine


