# Acorn SCSI Driver - Comprehensive Technical Notes

## TABLE OF CONTENTS
1. [BACKGROUND](#background)
2. [OVERVIEW](#overview)
3. [ARCHITECTURE & HARDWARE](#architecture--hardware)
4. [DATA STRUCTURES](#data-structures)
5. [DRIVER STATE MACHINE](#driver-state-machine)
6. [EXECUTION FLOW: USERSPACE TO HARDWARE](#execution-flow-userspace-to-hardware)
7. [KEY SUBSYSTEMS](#key-subsystems)
8. [INTERRUPTS & SYNCHRONIZATION](#interrupts--synchronization)
9. [DMA OPERATIONS](#dma-operations)
10. [DEBUG CAPABILITIES](#debug-capabilities)

---

## BACKGROUND

### Historical Context
- **Driver Author**: R.M. King
- **Architecture**: Acorn ARM-based RISC computer systems
- **Kernel Version**: Linux 2.6.20
- **History**: 
  - 26-Sep-1997: Queue module re-jigging, state machine refactor
  - 05-Oct-1997: Write support implementation
  - 12-Oct-1997: Interrupt re-entry catch added
  - 13-Dec-1998: Better abort code and command handling

### Important Design Decisions
- **Abandoned Select & Transfer Command**: Removed due to nasty race conditions and device errata
- **State Machine Basis**: Driver state, NOT SCSI state (easier debugging and control)
- **DMA vs PIO**: Driver is configured for DMA (USE_DMAC defined); PIO mode not currently supported
- **Tagged Queueing**: Initially disabled in CONFIG_SCSI_ACORNSCSI_TAGGED_QUEUE (untested)
- **Linked Commands**: Also disabled (higher level code doesn't support them)

### SCSI Controller & Interface Hardware
- **SCSI Controller**: WD33C93A (Western Digital 33C93A SCSI bus interface chip)
- **DMA Controller**: uPC71071 (Micro Peripherals DMA controller)
- **Bus Architecture**: EISA/Econet compatible (ARM-based Acorn RPC/CL7500)
- **Synchronous Transfer Support**: SDTR negotiation with configurable period (125-1020)

---

## OVERVIEW

### Purpose
The Acorn SCSI Driver (`acornscsi.c`) is a low-level SCSI host adapter driver that manages SCSI disk I/O operations for Acorn ARM-based computers. It bridges the Linux SCSI subsystem with the hardware SCSI controller (WD33C93A) and DMA controller (uPC71071).

### High-Level Goals
1. **Command Queuing**: Accept SCSI commands from the Linux block layer
2. **Device Communication**: Manage SCSI protocol handshakes and phases
3. **Data Transfer**: Perform DMA-based data transfers to/from devices
4. **Error Handling**: Detect and recover from SCSI errors and timeouts
5. **Command Completion**: Return results to the Linux SCSI subsystem

### Key Design Philosophy
```
USERSPACE (Block Device Layer)
           ↓
      [SCSI Subsystem]
           ↓
    [Acorn SCSI Driver] ← Coordination via state machine
           ↓
      [WD33C93A Chip]  ← Hardware SCSI controller
      [uPC71071 DMA]   ← Hardware DMA engine
           ↓
    [SCSI Bus]
           ↓
  [SCSI Devices]
```

The driver implements a **state machine** driven by interrupts. Rather than blocking,
the driver transitions between phases in response to hardware interrupts, allowing
asynchronous command completion and device disconnection/reconnection.

---

## ARCHITECTURE & HARDWARE

### WD33C93A SCSI Controller
- **Registers**: 27 main registers (0-26)
- **Function**: SCSI bus phase management, command execution, status reporting
- **Key Registers**:
  - `SBIC_CMND` (0x18): Command register (SELECT, TRANSFER, etc.)
  - `SBIC_SSR` (0x17): SCSI Status Register (phase information)
  - `SBIC_ASR` (0x1A): Auxiliary Status Register (interrupt pending, busy, etc.)
  - `SBIC_DATA` (0x19): Data port for SCSI protocol bytes
  - `SBIC_TARGETLUN` (0x0F): Target ID and LUN configuration

### uPC71071 DMA Controller
- **Channels**: 4 DMA channels (0-3)
- **Used by Driver**: Channel 0 for SCSI transfers
- **Modes**: Demand, single, block, or cascade transfer modes
- **Key Registers**:
  - `DMAC_TXCNTLO/TXCNTHI`: Transfer count (bytes to transfer)
  - `DMAC_TXADRLO/TXADRMD/TXADRHI`: Transfer address (24-bit)
  - `DMAC_MODECON`: Mode control (READ, WRITE, AUTOINIT, BLOCK)
  - `DMAC_DEVCON0/DEVCON1`: Device control (DMA handshake, timing)
  - `DMAC_STATUS`: Status register (transfer complete, request pending)

### Memory Layout
```
┌─────────────────────────────┐
│  Main System Memory         │
│  (Linux Buffers)            │
│  - User pages               │
│  - Kernel buffers           │
└─────────────────────────────┘
           ↑↓ DMA
┌─────────────────────────────┐
│  Card Internal Memory       │
│  (POD_SPACE: 0xD0000)       │
│  - DMA bounce buffers       │
└─────────────────────────────┘
           ↑↓ SCSI Bus
┌─────────────────────────────┐
│  SCSI Devices               │
│  (Disks, CDROMs, etc.)      │
└─────────────────────────────┘
```

### Interrupt Structure
- **IRQ Source 1** (bit 0): DMA controller interrupt (DMAC_STATUS)
- **IRQ Source 2** (bit 1): Reserved
- **IRQ Source 3** (bit 3): SCSI controller (WD33C93A) interrupt

---

## DATA STRUCTURES

### Main Host Adapter Structure: `AS_Host`

```c
typedef struct acornscsi_hostdata {
    // Core SCSI adapter information
    struct Scsi_Host *host;              // Pointer to Linux SCSI host
    struct scsi_cmnd *SCpnt;             // Currently executing command
    struct scsi_cmnd *origSCpnt;         // Original command (for reconnect)
    
    // Driver-specific SCSI state
    struct {
        unsigned int io_port;            // WD33C93A register base address
        unsigned int irq;                // Interrupt number
        phase_t phase;                   // Current SCSI phase (state machine)
        
        struct {
            unsigned char target;        // Reconnected target ID
            unsigned char lun;           // Reconnected LUN
            unsigned char tag;           // Reconnected command tag
        } reconnected;
        
        struct scsi_pointer SCp;         // Current data pointer & residual count
        MsgQueue_t msgs;                 // Message queue for MSG OUT/IN
        unsigned short last_message;     // Last message sent
        unsigned char disconnectable:1;  // Device allowed to disconnect
    } scsi;
    
    // Performance statistics
    struct {
        unsigned int queues;             // Commands queued
        unsigned int removes;            // Commands removed from queue
        unsigned int fins;               // Commands finished
        unsigned int reads;              // READ commands
        unsigned int writes;             // WRITE commands
        unsigned int miscs;              // MISC commands
        unsigned int disconnects;        // Disconnections
        unsigned int aborts;             // Aborts issued
        unsigned int resets;             // Bus resets
    } stats;
    
    // Command queuing
    struct {
        Queue_t issue;                   // Commands waiting to execute
        Queue_t disconnected;            // Commands disconnected (will reconnect)
    } queues;
    
    // Per-target device info
    struct {
        unsigned char sync_xfer;         // Synchronous transfer rate
        syncxfer_t sync_state;           // SDTR negotiation state
        unsigned char disconnect_ok:1;   // Device supports disconnect
    } device[8];
    unsigned long busyluns[64/sizeof(unsigned long)];  // Busy LUN bitmap
    
    // DMA controller state
    struct {
        unsigned int io_port;            // uPC71071 base address
        unsigned int io_intr_clear;      // DMA interrupt clear register
        unsigned int free_addr;          // Next free DMA address
        unsigned int start_addr;         // Start of current transfer
        dmadir_t direction;              // DMA direction (IN/OUT)
        unsigned int transferred;        // Bytes actually transferred
        unsigned int xfer_start;         // Scheduled transfer start address
        unsigned int xfer_length;        // Scheduled transfer length
        char *xfer_ptr;                  // Pointer to transfer buffer
        unsigned char xfer_required:1;   // Transfer pending
        unsigned char xfer_setup:1;      // DMA hardware configured
        unsigned char xfer_done:1;       // Transfer complete
    } dma;
    
    // Card-specific registers
    struct {
        unsigned int io_intr;            // Interrupt status register
        unsigned int io_page;            // Page register (reset, control)
        unsigned int io_ram;             // Card RAM base address
        unsigned char page_reg;          // Current page register value
    } card;
    
    // Debugging: circular status history buffer
    unsigned char status_ptr[9];
    struct status_entry status[9][STATUS_BUFFER_SIZE];  // 32-entry ring buffers
} AS_Host;
```

### SCSI Phase Enumeration

```c
typedef enum {
    PHASE_IDLE,              // Not processing anything
    PHASE_CONNECTING,        // Selecting target device
    PHASE_CONNECTED,         // Selected, waiting for bus phase
    PHASE_MSGOUT,            // Sending identification/control messages
    PHASE_RECONNECTED,       // Target reselected us
    PHASE_COMMANDPAUSED,     // Command partially sent
    PHASE_COMMAND,           // Sending command bytes to device
    PHASE_DATAOUT,           // Transferring data TO device
    PHASE_DATAIN,            // Receiving data FROM device
    PHASE_STATUSIN,          // Reading status byte
    PHASE_MSGIN,             // Receiving messages from device
    PHASE_DONE,              // Command completed successfully
    PHASE_ABORTED,           // Command aborted
    PHASE_DISCONNECT,        // Disconnecting from bus
} phase_t;
```

### Interrupt Return Status

```c
typedef enum {
    INTR_IDLE,               // No more work, exit interrupt handler
    INTR_NEXT_COMMAND,       // Finished command, try next one
    INTR_PROCESSING,         // Continue processing in IRQ handler
} intr_ret_t;
```

### Synchronous Transfer Negotiation State

```c
typedef enum {
    SYNC_ASYNCHRONOUS,       // Don't negotiate synchronous transfers
    SYNC_NEGOCIATE,          // Start negotiation
    SYNC_SENT_REQUEST,       // SDTR request sent, awaiting reply
    SYNC_COMPLETED,          // Synchronous transfer negotiated
} syncxfer_t;
```

### Command Type Classification

```c
typedef enum {
    CMD_READ,                // READ_6, READ_10, READ_12
    CMD_WRITE,               // WRITE_6, WRITE_10, WRITE_12
    CMD_MISC,                // All other commands
} cmdtype_t;
```

### Data Direction Tracking

```c
typedef enum {
    DATADIR_IN,              // Data phase FROM device (READ)
    DATADIR_OUT              // Data phase TO device (WRITE)
} datadir_t;
```

---

## DRIVER STATE MACHINE

### State Transition Diagram

```
                          PHASE_IDLE
                             ↑ ↓
                             │ │
          PHASE_CONNECTING ←──→ acornscsi_kick()
                ↓              queues command
                │ 0x11 (SELECT success)
                ↓
        PHASE_CONNECTED ←── Check bus phase after SELECT
                ↓
         0x8E → PHASE_MSGOUT
                ↓
        Send IDENTIFY message
                ↓
         PHASE_COMMAND ← 0x8A (bus phase)
                ↓
        Send command bytes
                ↓
      PHASE_DATAOUT/DATAIN ← Check CDB
                ↓ (optional)
        DMA transfer in progress
                ↓
        PHASE_STATUSIN ← 0x8B
                ↓
        Read status byte
                ↓
         PHASE_MSGIN
                ↓
        Process messages (DISCONNECT, SAVE_PTR, etc.)
                ↓
         PHASE_DONE
                ↓
    acornscsi_done() → complete command
                ↓
         Return to PHASE_IDLE
```

### Critical SSR (Status) Values

| SSR   | Meaning                     | Handler             | Next Phase           |
|-------|-----------------------------|--------------------|----------------------|
| 0x00  | Reset (std mode)            | Reinit SBIC         | PHASE_IDLE           |
| 0x01  | Reset (advanced mode)       | Normal init         | PHASE_IDLE           |
| 0x11  | Selection successful        | Update phase        | PHASE_CONNECTED      |
| 0x8a  | Bus: COMMAND phase          | Send CMD bytes      | PHASE_COMMAND        |
| 0x8b  | Bus: STATUS phase           | Read status         | PHASE_STATUSIN       |
| 0x8e  | Bus: MESSAGE OUT            | Send MSG+IDENTIFY   | PHASE_MSGOUT         |
| 0x8f  | Bus: MESSAGE IN             | Read messages       | PHASE_MSGIN          |
| 0x81  | Reselection (during SELECT) | Reconnect handling  | PHASE_RECONNECTED    |
| 0x41  | Unexpected disconnect       | Abort command       | INTR_NEXT_COMMAND    |
| 0x42  | Select timeout              | Error completion    | INTR_NEXT_COMMAND    |
| 0x85  | Target disconnect           | Clean up            | INTR_NEXT_COMMAND    |

### Key Transitions & Triggers

1. **PHASE_IDLE → PHASE_CONNECTING**
   - **Trigger**: `acornscsi_queuecmd()` enqueues command
   - **Action**: Call `acornscsi_kick()` if phase is IDLE
   - **Hardware**: Write target ID to SBIC_DESTID, issue CMND_SELWITHATN

2. **PHASE_CONNECTING → PHASE_CONNECTED**
   - **Trigger**: SSR = 0x11 (selection successful)
   - **Action**: Clear message queue, note DMA baseline
   - **Hardware**: SBIC generates interrupt on next bus phase

3. **PHASE_CONNECTED → PHASE_MSGOUT**
   - **Trigger**: SSR = 0x8E (bus phase = MESSAGE OUT)
   - **Action**: Build messages (IDENTIFY + optional SDTR)
   - **Hardware**: Configure SBIC for message transmission

4. **PHASE_MSGOUT → Next Phase**
   - **Trigger**: Message transmission complete, bus phase changes
   - **Action**: Check SSR for next bus phase (usually COMMAND)

5. **PHASE_COMMAND → PHASE_DATAOUT/DATAIN**
   - **Trigger**: Command bytes sent, SSR shows DATA phase
   - **Action**: Configure DMA for data transfer
   - **Hardware**: Start DMA transfer via DMAC

6. **PHASE_DATAIN/DATAOUT → PHASE_STATUSIN**
   - **Trigger**: SSR = 0x8B (bus phase = STATUS)
   - **Action**: DMA transfer complete, read status byte

7. **PHASE_STATUSIN → PHASE_MSGIN**
   - **Trigger**: SSR = 0x8F (bus phase = MESSAGE IN)
   - **Action**: Read incoming messages
   - **Common Messages**: COMMAND COMPLETE, DISCONNECT, SAVE DATA POINTER

8. **PHASE_MSGIN → PHASE_DONE or PHASE_DISCONNECT**
   - **Trigger**: COMMAND COMPLETE or DISCONNECT message
   - **Action**: If DISCONNECT, save device state; if COMPLETE, finish command
   - **Result**: `acornscsi_done()` called

9. **During Any Phase → Reselection (SSR = 0x81)**
   - **Trigger**: Target reselects during command execution
   - **Action**: Save current command to `origSCpnt`, create disconnected queue entry
   - **Result**: Handle reselect, may restore previous command

---

## EXECUTION FLOW: USERSPACE TO HARDWARE

### Complete Request Path with Detailed Timing

#### Stage 1: Userspace Issues I/O Request

```
┌─────────────────────────────────────────────┐
│ User Application (e.g., read /dev/sda1)     │
│ - read(fd, buffer, 4096)                    │
│ - System Call: SYS_read                     │
└─────────────────────────────────────────────┘
           ↓ kernel transition
```

#### Stage 2: Linux Block Layer Processing

```
┌─────────────────────────────────────────────┐
│ VFS (Virtual File System Layer)             │
│ - File system calls fs->read_iter()         │
│ - Allocates bio (block I/O structure)       │
│ - Maps logical blocks to physical LBA       │
└─────────────────────────────────────────────┘
           ↓
┌─────────────────────────────────────────────┐
│ Block Device Layer                          │
│ - blk_queue_bio() queues bio request        │
│ - Elevator schedules requests               │
│ - Invokes disk->fops->submit_bio            │
└─────────────────────────────────────────────┘
           ↓
```

#### Stage 3: Linux SCSI Subsystem Entry

```
┌─────────────────────────────────────────────┐
│ SCSI Mid-Layer (sd.c - SCSI Disk Driver)   │
│ - sd_prep_fn() prepares SCSI command        │
│   * Converts LBA to CDB (Command Descriptor Block)
│   * For LBA 0, size 8: CDB = READ_10(lba=0, len=8)
│   * CDB bytes: [0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08]
│                                               ↑ READ_10 command
│ - Creates struct scsi_cmnd                  │
│ - Calls host->hostt->queuecommand()         │
└─────────────────────────────────────────────┘
           ↓
```

#### Stage 4: Acorn SCSI Driver - Command Queueing

```
┌─────────────────────────────────────────────┐
│ acornscsi_queuecmd()                        │
│ (file: drivers/scsi/arm/acornscsi.c:2518)  │
│                                              │
│ Function: Queue SCSI command for execution  │
│ Parameters:                                  │
│   - SCpnt: struct scsi_cmnd*                │
│   - done: completion callback               │
└─────────────────────────────────────────────┘

Task 1: Validate Command
  - Check if done() callback is provided
  - Enforce write protection (NO_WRITE bitmask)
  - For this test: READ_10 to device 0, LUN 0

Task 2: Initialize Command Structure
  SCpnt->scsi_done = done              /* Store callback address */
  SCpnt->host_scribble = NULL          /* Clear private field */
  SCpnt->result = 0                    /* Clear result (no error yet) */
  SCpnt->tag = 0                       /* No tagged queueing yet */
  SCpnt->SCp.phase = DATADIR_IN        /* Expect data from device (READ) */
  SCpnt->SCp.sent_command = 0          /* Haven't sent CDB yet */
  SCpnt->SCp.scsi_xferred = 0          /* No data transferred yet */
  
  init_SCp(SCpnt)                      /* Initialize scatter-gather pointer */
    - SCpnt->SCp.ptr = page address of buffer
    - SCpnt->SCp.this_residual = 4096  /* Bytes remaining in current segment */

Task 3: Add to Issue Queue
  queue_add_cmd_ordered(&host->queues.issue, SCpnt)
    - Insert into sorted issue queue
    - Queue order: typically by device ID, then LUN
    - Result: SCpnt now in host->queues.issue
    
Task 4: Attempt Immediate Start
  if (host->scsi.phase == PHASE_IDLE) {
      acornscsi_kick(host)             /* Try to start command immediately */
  }
  
  Statistics: host->stats.queues++

Return: 0 (success)
```

#### Stage 5: Acorn SCSI Driver - Command Dispatch

```
┌─────────────────────────────────────────────┐
│ acornscsi_kick()                            │
│ (file: drivers/scsi/arm/acornscsi.c:706)   │
│                                              │
│ Function: Dispatch next command to SCSI     │
│ Entry conditions: phase == PHASE_IDLE       │
│ Returns: INTR_PROCESSING (work started)     │
└─────────────────────────────────────────────┘

Step 1: Dequeue Command
  SCpnt = queue_remove_exclude(&host->queues.issue,
                               host->busyluns)
    - Get highest priority command
    - Exclude targets with busy LUNs
    - Result: SCpnt now points to our READ command

Step 2: Handle Current Command (if any)
  if (host->scsi.disconnectable && host->SCpnt) {
      queue_add_cmd_tail(&host->queues.disconnected,
                         host->SCpnt)    /* Save disconnected command */
      host->scsi.disconnectable = 0
  }

Step 3: Check for Pending Interrupts
  asr = sbic_arm_read(host->scsi.io_port, SBIC_ASR)
  if (asr & (ASR_INT | ASR_BSY | ASR_CIP))
      abort - interrupt pending or bus busy
  
  Otherwise, ready to select target

Step 4: Issue SCSI Selection Command
  sbic_arm_write(host->scsi.io_port, SBIC_DESTID,
                 SCpnt->device->id)          /* Set target ID = 0 */
  sbic_arm_write(host->scsi.io_port, SBIC_CMND,
                 CMND_SELWITHATN)            /* SELECT WITH ATTENTION */
                 
  This command tells WD33C93A to:
    1. Arbitrate for SCSI bus
    2. Select target 0
    3. Assert ATN (Attention) line
    4. Generate interrupt when selection completes or times out

Step 5: Update Driver State
  host->scsi.phase = PHASE_CONNECTING
  host->SCpnt = SCpnt               /* Current command */
  host->scsi.SCp = SCpnt->SCp       /* Copy data pointer */
  host->dma.xfer_setup = 0          /* DMA not configured yet */
  host->dma.xfer_required = 0
  host->dma.xfer_done = 0
  
  set_bit(target*8 + lun, host->busyluns)  /* Mark LUN as busy */
  
  Statistics updates:
    host->stats.removes++           /* Removed from queue */
    host->stats.reads++             /* READ command */

Return: INTR_PROCESSING
  - Means: hardware work started, expect interrupt
```

#### Stage 6: Hardware SCSI Bus Phase 1 - SELECTION

```
Timeline: SCSI Bus Operations
──────────────────────────────────────────────────

T0: WD33C93A receives CMND_SELWITHATN
    - Arbitration phase (starts immediately)
    - WD33C93A places this adapter's ID on SCSI bus
    - Waits for all higher IDs to drop off

T1: Arbitration won
    - Select phase begins
    - Drive lines: DB[7:0] = target ID (0x01 << target_id)
    - For target 0: drives 0x01 on bus
    - ATN line asserted (attention requested)

T2: Target 0 recognizes its ID
    - Responds by asserting ACK
    - WD33C93A releases bus

T3: Target responds (BSY + SEL deasserted)
    - Target is now selected
    - SCSI bus now in MESSAGE OUT phase
    - WD33C93A generates interrupt:
      SSR = 0x11 (SELECTED, now executing)

T4: Interrupt delivered to ARM CPU
    - IRQ line triggered
    - Linux calls acornscsi_intr()
```

#### Stage 7: Acorn SCSI Driver - Interrupt Handler (First)

```
┌─────────────────────────────────────────────┐
│ acornscsi_intr() [Entry 1: Selection OK]    │
│ (file: drivers/scsi/arm/acornscsi.c:2470)  │
│                                              │
│ IRQ Handler triggered by WD33C93A interrupt │
└─────────────────────────────────────────────┘

Step 1: Check Interrupt Source
  iostatus = inb(host->card.io_intr)    /* Read interrupt status register */
  
  Possible bits:
    bit 0: (reserved)
    bit 1: (reserved)
    bit 2: DMA interrupt pending
    bit 3: SBIC/SCSI interrupt pending ← Set now
    
  iostatus = 0x08 (only SBIC bit set)

Step 2: Check for DMA Interrupt
  if (iostatus & 2)
      acornscsi_dma_intr(host)      /* Not called (bit 1 not set) */
  
  iostatus = inb(host->card.io_intr) /* Re-read */

Step 3: Process SCSI Interrupt
  if (iostatus & 8)                 /* Bit 3 set */
      ret = acornscsi_sbicintr(host, in_irq=0)
      
  This calls the main SCSI state machine handler
```

#### Stage 8: SCSI State Machine - Phase: CONNECTING

```
┌─────────────────────────────────────────────┐
│ acornscsi_sbicintr() [SSR=0x11 connected]   │
│ (file: drivers/scsi/arm/acornscsi.c:1979)  │
│                                              │
│ Process SCSI interrupt based on phase      │
└─────────────────────────────────────────────┘

Current State: host->scsi.phase = PHASE_CONNECTING
Current SSR: 0x11 (BUS FREE -> SELECTION)

Step 1: Read Status Registers
  asr = sbic_arm_read(host->scsi.io_port, SBIC_ASR)
    - Check if interrupt actually pending: ASR_INT set ✓
  
  ssr = sbic_arm_read(host->scsi.io_port, SBIC_SSR)
    - Read status: ssr = 0x11 ✓

Step 2: Log Status to Debug Ring Buffer
  ADD_STATUS(8, ssr=0x11, phase=CONNECTING, in_irq=0)
    - Store in circular buffer for post-mortem analysis
    - Timestamp: jiffies
    - Used if kernel panics or command hangs

Step 3: PHASE_CONNECTING Handler
  
  switch (host->scsi.phase) {
  case PHASE_CONNECTING:
      switch (ssr) {
      case 0x11:                    /* BUS FREE -> SELECTION success */
          ✓ This branch taken
          
          Step 3a: Update Phase
          host->scsi.phase = PHASE_CONNECTED
          
          Step 3b: Reset Message Queue
          msgqueue_flush(&host->scsi.msgs)
            - Clear any pending messages to send
          
          Step 3c: Snapshot DMA Transfer Count
          host->dma.transferred = host->scsi.SCp.scsi_xferred
            - Initially 0 (no data transferred yet)
            - Used to detect spurious DMA completions
          
          Step 3d: Check for Immediate Interrupt
          asr = sbic_arm_read(host->scsi.io_port, SBIC_ASR)
          
          if (!(asr & ASR_INT))      /* No more interrupts pending */
              break                  /* Exit interrupt handler */
          
          /* If another interrupt is pending, process it now */
          ssr = sbic_arm_read(host->scsi.io_port, SBIC_SSR)
          
          /* Note: In typical scenario, bus now in MESSAGE OUT phase
             SSR might already be 0x8E (MESSAGE OUT phase) */
          
          goto connected;            /* Handle next phase */
      }
  }
  
Return: INTR_PROCESSING
```

#### Stage 9: SCSI Bus Phase 2 - MESSAGE OUT

```
Timeline: Message Phase
──────────────────────

After selection completes:
  - Target device has control of SCSI bus
  - Target enters MESSAGE OUT phase (unexpected for host)
  - Target deasserts SEL, asserts BSY, ACK, and REQ
  - Data lines set to 0x00 (no message byte yet)

WD33C93A detects MESSAGE OUT phase:
  - Sets SSR = 0x8E (PHASE=MESSAGE OUT, PROCESS=MESSAGE)
  - Asserts interrupt

Host driver (PHASE_CONNECTED state):
  - Reads SSR = 0x8E
  - Recognizes MESSAGE OUT phase during PHASE_CONNECTED
  - Must respond with IDENTIFY message + optional SDTR
```

#### Stage 10: SCSI State Machine - MESSAGE OUT Handler

```
┌─────────────────────────────────────────────┐
│ acornscsi_sbicintr() [SSR=0x8E MESSAGE OUT] │
│ (continuing from previous interrupt)        │
│                                              │
│ Process MESSAGE OUT phase                   │
└─────────────────────────────────────────────┘

Current State: host->scsi.phase = PHASE_CONNECTED
Current SSR: 0x8E (PHASE=MESSAGE OUT, PROCESS=MESSAGE)

Step 1: Match Phase Handler
  switch (host->scsi.phase) {
  case PHASE_CONNECTED:
      switch (ssr) {
      case 0x8E:                    /* PHASE=MESSAGE OUT */
          ✓ This branch taken
          
          Step 1a: Update Phase
          host->scsi.phase = PHASE_MSGOUT
          
          Step 1b: Build Outgoing Messages
          acornscsi_buildmessages(host)
            
            Message Building Process:
            ─────────────────────────
            
            msgqueue_add_byte(&host->scsi.msgs, 0x80 | lun)
              - Add IDENTIFY message
              - Byte value: 0x80 (IDENTIFY bit) | 0x00 (LUN)
              - Result: msg = 0x80
            
            if (host->device[target].sync_state == SYNC_NEGOCIATE) {
                /* First contact with this device */
                host->device[target].sync_state = SYNC_SENT_REQUEST
                
                /* Build SDTR (Synchronous Data Transfer Request) message */
                msgqueue_add_byte(&host->scsi.msgs, 0x01)  /* Extended msg */
                msgqueue_add_byte(&host->scsi.msgs, 0x03)  /* SDTR length */
                msgqueue_add_byte(&host->scsi.msgs, 0x01)  /* SDTR code */
                msgqueue_add_byte(&host->scsi.msgs, 125)   /* Period (125 ns) */
                msgqueue_add_byte(&host->scsi.msgs, 12)    /* Offset (12) */
                
                Result: Queue now contains:
                  [0x80, 0x01, 0x03, 0x01, 0x7D, 0x0C]
            }
          
          Step 1c: Send First Message Byte
          acornscsi_sendmessage(host)
            
            Logic:
            ──────
            Extract first byte from message queue: 0x80
            Write to SBIC:
              sbic_arm_write(host->scsi.io_port, SBIC_DATA, 0x80)
              
            Issue SBIC command:
              sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_XFERINFO)
              
            Hardware will:
              1. Place 0x80 on SCSI data lines
              2. Assert ACK when target reads byte
              3. Generate interrupt when complete

Return: INTR_PROCESSING (more work in progress)
```

#### Stage 11: Message Transmission & COMMAND Phase Transition

```
Timeline: Message Transmission
────────────────────────────────

T0: Driver writes 0x80 to SBIC_DATA, issues XFERINFO
    - SBIC places 0x80 on SCSI bus
    - Target sees IDENTIFY message

T1: Target strobes ACK
    - Acknowledges message byte
    - SBIC generates interrupt: SSR = 0x20 (/ACK asserted)

T2: Driver reads interrupt, updates message queue state
    - Remove 0x80 from queue front
    - Check if more messages pending
    - If SDTR pending: send extended message bytes
    - Eventually: all messages sent

T3: Target enters COMMAND phase
    - Takes control of bus
    - Asserts REQ for command bytes
    - SBIC detects phase change: SSR = 0x8A (COMMAND phase)
    - Generates interrupt
```

#### Stage 12: COMMAND Phase - Send CDB

```
┌─────────────────────────────────────────────┐
│ acornscsi_sbicintr() [SSR=0x8A COMMAND]     │
│                                              │
│ Process COMMAND phase (send CDB)            │
└─────────────────────────────────────────────┘

Current State: host->scsi.phase = PHASE_MSGOUT (or transitioning)
Current SSR: 0x8A (PHASE=COMMAND, 10-byte CDB)

Step 1: Detect Command Phase
  case 0x8a:                        /* Bus phase: COMMAND */
      
      Step 1a: Update Driver Phase
      host->scsi.phase = PHASE_COMMAND
      
      Step 1b: Send Command Descriptor Block
      acornscsi_sendcommand(host)
      
        Task: Transmit CDB bytes to device
        
        CDB Contents (READ_10):
        ──────────────────────
        Byte 0:   0x28  (READ_10 command opcode)
        Byte 1:   0x00  (rel addr=0, FUA=0, DPO=0)
        Byte 2-5: 0x00000000  (LBA = 0, big-endian)
        Byte 6:   0x00  (reserved)
        Byte 7-8: 0x0008  (Transfer length = 8 blocks, big-endian)
        Byte 9:   0x00  (control byte)
        
        Total: 10 bytes
        
        Implementation Detail:
        ──────────────────
        WD33C93A has internal FIFO. Driver can:
          Option A: Write each byte separately (slow, many interrupts)
          Option B: Use block transfer mode via DMA (fast, fewer interrupts)
        
        This driver uses: Option A (individual byte writes)
        
        Loop through CDB:
        for (i = 0; i < 10; i++) {
            sbic_arm_write(host->scsi.io_port, SBIC_DATA,
                          SCpnt->cmnd[i])
        }
        
        Issue XFERINFO to send all bytes:
        sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_XFERINFO)

Return: INTR_PROCESSING
```

#### Stage 13: DATA IN Phase - DMA Setup

```
Timeline: CDB Transmission & Phase Change
────────────────────────────────────────────

T0: Driver sends CDB bytes
    - SBIC transmits 0x28, 0x00, 0x00, ..., 0x00

T1: Target receives CDB
    - Processes READ_10 command
    - Prepares to send data
    - Takes control of bus
    - Enters DATA IN phase

T2: WD33C93A detects DATA IN phase
    - Sets SSR = 0x89 (PHASE=DATA_IN, PROCESS=1)
    - Generates interrupt

T3: Driver detects DATA IN phase
    - Must configure DMA for data transfer
```

#### Stage 14: DATA IN Phase - DMA Configuration

```
┌─────────────────────────────────────────────┐
│ acornscsi_sbicintr() [SSR=0x89 DATA_IN]     │
│                                              │
│ Process DATA IN phase (DMA setup)           │
└─────────────────────────────────────────────┘

Current State: host->scsi.phase = PHASE_COMMAND
Current SSR: 0x89 (PHASE=DATA_IN)

Step 1: Detect DATA IN Phase
  case 0x89:                        /* Bus phase: DATA IN */
      
      Step 1a: Update Driver Phase
      host->scsi.phase = PHASE_DATAIN
      
      Step 1b: Setup DMA Transfer
      acornscsi_dma_setup(host)
      
        DMA Configuration Steps:
        ───────────────────────
        
        1. Determine transfer parameters from command:
           - SCpnt->request_bufflen = 4096 (bytes to read)
           - SCpnt->request_buffer = kernel buffer address
           
        2. Configure DMAC for READ (device → memory):
        
           Direction: host->dma.direction = DMA_IN
           
           Address: host->dma.xfer_start = buffer physical address
           
           Length: host->dma.xfer_length = 4096 bytes
           
        3. Program uPC71071 DMA Controller:
        
           dmac_write(host->dma.io_port, DMAC_CHANNEL, 0)
             - Select DMA channel 0 (SCSI)
           
           dmac_write(host->dma.io_port, DMAC_TXADRLO,
                      (addr >> 0) & 0xFF)
           dmac_write(host->dma.io_port, DMAC_TXADRMD,
                      (addr >> 8) & 0xFF)
           dmac_write(host->dma.io_port, DMAC_TXADRHI,
                      (addr >> 16) & 0xFF)
             - Program 24-bit DMA address
           
           dmac_write(host->dma.io_port, DMAC_TXCNTLO,
                      (4096 >> 0) & 0xFF)
           dmac_write(host->dma.io_port, DMAC_TXCNTHI,
                      (4096 >> 8) & 0xFF)
             - Program transfer count
           
           dmac_write(host->dma.io_port, DMAC_MODECON,
                      MODECON_READ | MODECON_BLOCK | INIT_MODECON)
             - Mode: READ (device→memory)
             - Block transfer (efficient bulk transfers)
           
           dmac_write(host->dma.io_port, DMAC_DEVCON0, INIT_DEVCON0)
             - Device control: timing, handshake, burst
           
           dmac_write(host->dma.io_port, DMAC_MASKREG,
                      MASKREG_M1 | MASKREG_M2 | MASKREG_M3)
             - Unmask channel 0 for DMA requests
        
        4. Connect WD33C93A to DMA:
        
           sbic_arm_write(host->scsi.io_port, SBIC_CTRL,
                          INIT_SBICDMA | CTRL_IDI)
             - Enable DMA mode in SBIC
             - IDI = Interrupt on Disconnect
           
           sbic_arm_write(host->scsi.io_port, SBIC_CMND,
                          CMND_XFERINFO)
             - Start data transfer from WD33C93A
      
      Step 1c: Mark DMA as Active
      host->dma.xfer_setup = 1
      host->dma.xfer_required = 0  /* Already started */

Return: INTR_PROCESSING
```

#### Stage 15: Hardware Data Transfer - DMA Active

```
Timeline: DMA Data Transfer (4096 bytes)
─────────────────────────────────────────────

T0: Driver enables DMA in both controllers
    - WD33C93A configured to accept DMA requests
    - uPC71071 configured to read from bus and write to memory
    - Both waiting for data handshake

T1-T_n: SCSI Bus Data Phase
    
    For each data byte:
    1. Target asserts REQ (request for strobe)
    2. WD33C93A recognizes REQ
    3. WD33C93A asserts DMAREQ to uPC71071
    4. uPC71071 reads SCSI data lines
    5. uPC71071 writes byte to memory at programmed address
    6. uPC71071 increments address counter
    7. uPC71071 decrements byte counter
    8. WD33C93A strobes ACK back to target
    9. Target de-asserts REQ
    10. Loop continues for next byte

    This happens autonomously (no CPU intervention!)
    
    Transfer Rate: Depends on target speed
    - Modern disks: 5-10 MB/s over SCSI
    - 4096 bytes: ~0.4-0.8 milliseconds
    
    CPU continues running other tasks during transfer
    (I/O wait is hidden via interrupt-driven operation)

T_complete: Last byte transferred
    - uPC71071 byte counter reaches 0
    - uPC71071 sets STATUS_TC0 bit (Terminal Count)
    - uPC71071 asserts DMAIRQ interrupt to ARM CPU
    - WD33C93A still in DATA_IN phase
```

#### Stage 16: DMA Complete - First Interrupt

```
┌─────────────────────────────────────────────┐
│ acornscsi_intr() [DMA Complete]             │
│ (file: drivers/scsi/arm/acornscsi.c:2470)  │
│                                              │
│ Interrupt: DMA transfer finished            │
│ (4096 bytes now in memory)                  │
└─────────────────────────────────────────────┘

Step 1: Check Interrupt Source
  iostatus = inb(host->card.io_intr)
    - iostatus = 0x02 (DMA bit set, SBIC bit clear)

Step 2: Process DMA Interrupt
  if (iostatus & 2) {                /* DMA interrupt pending */
      acornscsi_dma_intr(host)
      iostatus = inb(host->card.io_intr)
  }
  
  Inside acornscsi_dma_intr():
  ───────────────────────────
  status = dmac_read(host->dma.io_port, DMAC_STATUS)
  
  if (status & STATUS_TC0) {        /* Terminal count reached */
      /* DMA transfer complete */
      
      /* Update statistics */
      host->dma.transferred = 4096  /* All bytes transferred */
      
      /* Disable DMA */
      dmac_write(host->dma.io_port, DMAC_MASKREG, MASK_ON)
                /* Mask all channels */
      
      /* Note: Don't disable SBIC DMA yet - target may still be
         sending data, or entering next phase */
      
      /* Set flag for next interrupt */
      host->dma.xfer_done = 1

Step 3: Check for SBIC Interrupt
  iostatus = inb(host->card.io_intr)  /* Re-read */
  
  if (iostatus & 8) {               /* SBIC interrupt pending */
      ret = acornscsi_sbicintr(host, in_irq=1)
  } else {
      return INTR_IDLE              /* No more work */
  }

Return: Depends on SBIC status
```

#### Stage 17: SBIC Interrupt After DMA - STATUS Phase

```
Timeline: Target Completion Sequence
───────────────────────────────────

After all 4096 bytes transferred:
  - Target has sent all data
  - Takes control of bus
  - Enters STATUS phase
  - Places status byte on data lines: 0x00 (GOOD)
  - Asserts REQ

WD33C93A detects STATUS phase:
  - Sets SSR = 0x8B (PHASE=STATUS, PROCESS=1)
  - Asserts interrupt

Host driver:
  - Reads SSR = 0x8B
  - Must read status byte
```

#### Stage 18: STATUS IN Phase

```
┌─────────────────────────────────────────────┐
│ acornscsi_sbicintr() [SSR=0x8B STATUS_IN]  │
│                                              │
│ Process STATUS_IN phase                     │
└─────────────────────────────────────────────┘

Current State: host->scsi.phase = PHASE_DATAIN
Current SSR: 0x8B (PHASE=STATUS_IN)

Step 1: Detect STATUS_IN Phase
  case 0x8b:                        /* PHASE=STATUS_IN */
      
      Step 1a: Update Driver Phase
      host->scsi.phase = PHASE_STATUSIN
      
      Step 1b: Read Status Byte
      acornscsi_readstatusbyte(host)
      
        /* Read from SCSI data lines via SBIC */
        status = sbic_arm_read(host->scsi.io_port, SBIC_DATA)
        status = 0x00  /* GOOD status */
        
        /* Store in command structure */
        host->scsi.SCp.Status = status
        
        /* Configure SBIC to receive and strobe ACK */
        sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_XFERINFO)
      
      Step 1c: Disable DMA (we're no longer transferring data)
      sbic_arm_write(host->scsi.io_port, SBIC_CTRL,
                     INIT_SBIC | CTRL_IDI)
                /* Disable DMAMODE */

Return: INTR_PROCESSING (expect MESSAGE IN next)
```

#### Stage 19: MESSAGE IN Phase - Command Complete

```
Timeline: Status Transmission & Message Phase
──────────────────────────────────────────────

T0: Driver reads status byte (0x00)
    - SBIC strobes ACK
    - Target sees ACK

T1: Target de-asserts REQ
    - Prepares next phase
    - Enters MESSAGE IN phase
    - Places 0x00 (COMMAND COMPLETE message)

T2: WD33C93A detects MESSAGE IN phase
    - Sets SSR = 0x8F (PHASE=MESSAGE_IN)
    - Asserts interrupt

T3: Driver enters MESSAGE IN handler
```

#### Stage 20: MESSAGE IN - COMMAND COMPLETE

```
┌─────────────────────────────────────────────┐
│ acornscsi_sbicintr() [SSR=0x8F MESSAGE_IN]  │
│                                              │
│ Process MESSAGE_IN phase                    │
└─────────────────────────────────────────────┘

Current State: host->scsi.phase = PHASE_STATUSIN
Current SSR: 0x8F (PHASE=MESSAGE_IN)

Step 1: Detect MESSAGE_IN Phase
  case 0x8f:                        /* PHASE=MESSAGE_IN */
      
      Step 1a: Update Driver Phase
      host->scsi.phase = PHASE_MSGIN
      
      Step 1b: Read Message Byte
      message = sbic_arm_read(host->scsi.io_port, SBIC_DATA)
      message = 0x00  /* COMMAND COMPLETE message */
      
      /* Store in scsi_pointer for later processing */
      host->scsi.SCp.Message = message
      
      Step 1c: Acknowledge Message
      sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_XFERINFO)
        - Strobes ACK to target
      
      Step 1d: Process Message Based on Type
      
      switch (message) {
      case 0x00:                    /* COMMAND COMPLETE */
          ✓ This case taken
          
          /* Command finished successfully */
          /* Bus will go to BUS FREE after target de-asserts BSY */
          
          Step 1e: Mark Command Done
          acornscsi_done(host, &host->SCpnt, DID_OK)
      }

Return: INTR_IDLE (no more work, wait for next command)
```

#### Stage 21: Command Completion - Notify Upper Layers

```
┌─────────────────────────────────────────────┐
│ acornscsi_done()                            │
│ (file: drivers/scsi/arm/acornscsi.c:805)   │
│                                              │
│ Finalize command and notify SCSI subsystem  │
└─────────────────────────────────────────────┘

Task 1: Disable SCSI Hardware
  sbic_arm_write(host->scsi.io_port, SBIC_SOURCEID,
                 SOURCEID_ER | SOURCEID_DSP)
    - Clear interrupts, allow external resets
    - Disassert any control lines

Task 2: Update Statistics
  host->stats.fins++                /* Increment finish count */

Task 3: Build SCSI Result Code
  SCpnt->result = (DID_OK << 16) | (message << 8) | status
  
  Components:
  - DID_OK << 16:   Driver result (0x00000000)
  - message << 8:   COMMAND COMPLETE (0x00000000)
  - status:         Device status byte (0x00 = GOOD)
  
  Result: SCpnt->result = 0x00000000 (SUCCESS!)

Task 4: Verify Data Transfer Completed
  if (result == DID_OK) {
      /* Check that promised data was actually transferred */
      
      if (SCpnt->underflow == 0) {
          if (host->scsi.SCp.ptr &&
              acornscsi_cmdtype(SCpnt->cmnd[0]) != CMD_MISC) {
              /* Error: Data expected but none transferred */
          }
      } else {
          /* Verify minimum transfer (SCpnt->underflow) met */
          if (host->scsi.SCp.scsi_xferred < SCpnt->underflow ||
              host->scsi.SCp.scsi_xferred !=
              host->dma.transferred) {
              /* Mismatch between requested and actual transfer */
          }
      }
  }

Task 5: Update Phase to IDLE
  host->scsi.phase = PHASE_IDLE
  host->SCpnt = NULL
  host->scsi.disconnectable = 0

Task 6: Call Completion Callback
  SCpnt->scsi_done(SCpnt)
  
  This calls back to the SCSI mid-layer:
  → sd.c (disk driver) - notify command complete
  → bio completion handlers
  → call bio->bi_end_io(bio, error)
  → eventually notify filesystem and wake userspace
```

#### Stage 22: SCSI Mid-Layer Response Processing

```
┌─────────────────────────────────────────────┐
│ SCSI Mid-Layer (scsi_dispatch.c, sd.c)     │
│                                              │
│ Process command result                      │
└─────────────────────────────────────────────┘

Step 1: Receive Result from Device
  scsi_done(SCpnt) called with:
    - SCpnt->result = 0x00000000 (SUCCESS)

Step 2: Decode Result
  status = (SCpnt->result >> 0) & 0xFF    /* Status byte: 0x00 */
  msg = (SCpnt->result >> 8) & 0xFF       /* Message: 0x00 */
  driver_byte = (SCpnt->result >> 16) & 0xFF  /* Driver result: 0x00 */
  
  All zeros indicate complete success

Step 3: Update Block Layer Statistics
  - Mark request as complete
  - Update sector read counter
  - Notify elevator scheduler

Step 4: Invoke Request Completion Handler
  - Return bio to filesystem
  - Data now available in kernel buffer
  - Page cache updated with disk data

Step 5: Wake Filesystem & User Threads
  - Filesystem (ext3, ext4) processes page completion
  - File system updates inode/dentry caches
  - Wakes any threads blocked on read
```

#### Stage 23: Userspace - Data Available

```
┌─────────────────────────────────────────────┐
│ User Application Thread (resumed)           │
│                                              │
│ Read syscall returns to userspace           │
└─────────────────────────────────────────────┘

Step 1: Syscall Returns
  read(fd, buffer, 4096) returns 4096
    - All requested bytes successfully transferred
    - buffer[] now contains disk data (bytes 0-4095 of file)

Step 2: User Code Processes Data
  /* User application can now read buffer[0..4095] */
  
  for (i = 0; i < 4096; i++) {
      process_byte(buffer[i]);
  }

Timeline Summary:
─────────────────────────────────────────────

Userspace syscall entry
  ↓ (~0 ms, kernel entry)
  VFS route lookup + allocation
  ↓ (~0.1 ms)
  SCSI subsystem CDB preparation
  ↓ (~0.1 ms)
  Acorn driver: queue command
  ↓ (~0 ms, queued)
  Acorn driver: select target (SCSI arbitration/selection)
  ↓ (~0.3 ms, SCSI bus negotiation)
  Acorn driver: send identification & SDTR messages
  ↓ (~0.1 ms)
  Acorn driver: send CDB (READ_10)
  ↓ (~0.1 ms)
  Target device: seek + read data
  ↓ (~5-10 ms, mechanical latency - largest delay!)
  DMA transfer: 4096 bytes via hardware DMA
  ↓ (~0.4 ms, at typical 10 MB/s)
  Status and completion messages
  ↓ (~0.1 ms)
  Acorn driver: complete command notification
  ↓ (~0.1 ms)
  SCSI mid-layer: update block layer
  ↓ (~0.1 ms)
  Filesystem: update caches, wake threads
  ↓ (~0 ms, context switch)
  Userspace syscall return
  ↓ (~0 ms, user code continues)
  
TOTAL: ~6-15 ms (mostly mechanical seek time of actual disk!)

CPU time actually used: ~1-2 ms
(The rest is I/O wait - other processes run during disk latency)
```

---

## KEY SUBSYSTEMS

### 1. Command Queueing System

#### Issue Queue
- **Purpose**: Holds commands awaiting execution
- **Structure**: Priority queue ordered by target ID, then LUN
- **Operations**:
  - `queue_add_cmd_ordered()`: Insert command maintaining order
  - `queue_remove_exclude()`: Dequeue respecting busy LUN mask

#### Disconnected Queue
- **Purpose**: Holds commands that disconnected from bus
- **Structure**: FIFO queue
- **Rationale**: SCSI devices can disconnect and reconnect later
- **Reselection**: Device will reselect adapter on bus later

### 2. Message Handling

#### Message Queue (`MsgQueue_t`)
- Stores message bytes to send (IDENTIFY, SDTR, etc.)
- IDENTIFY: `0x80 | LUN` (8-bit code)
- SDTR (Sync Data Transfer Request): 6-byte extended message
- Operations:
  - `msgqueue_add_byte()`: Queue outgoing message byte
  - `msgqueue_flush()`: Clear queue (reset on phase change)
  - `msgqueue_getmsg()`: Extract bytes

#### Message Byte Types
- `0x00`: COMMAND COMPLETE
- `0x01`: EXTENDED MESSAGE (marker for 2-byte header + payload)
- `0x02`: SAVE DATA POINTER (checkpoint transfer progress)
- `0x03`: RESTORE DATA POINTER (resume from checkpoint)
- `0x04`: DISCONNECT
- `0x06`: ABORT
- `0x80 | LUN`: IDENTIFY (LUN in lower 3 bits)

### 3. DMA Subsystem

#### DMA Transfer States
```c
struct {
    unsigned int io_port;            // uPC71071 base
    unsigned int free_addr;          // Next available memory
    unsigned int start_addr;         // Current transfer start
    unsigned int transferred;        // Bytes completed
    unsigned int xfer_start;         // Scheduled transfer start
    unsigned int xfer_length;        // Scheduled transfer length
    char *xfer_ptr;                  // Buffer pointer
    
    dmadir_t direction;              // IN (device→memory) or OUT
    unsigned char xfer_required:1;   // Transfer scheduled
    unsigned char xfer_setup:1;      // DMA hw configured
    unsigned char xfer_done:1;       // Terminal count reached
} dma;
```

#### DMA Transfer Sequence
1. **Setup Phase**: Configure uPC71071 with address, count, mode
2. **Start Phase**: Enable DMA in both SBIC and uPC71071
3. **Active Phase**: Hardware autonomously transfers bytes
4. **Complete Phase**: uPC71071 asserts interrupt at terminal count
5. **Cleanup Phase**: Disable DMA, handle any residual bytes

### 4. Synchronous Transfer Negotiation

#### SDTR (Synchronous Data Transfer Request)
- **Purpose**: Negotiate faster transfer rates
- **Target Speed**: Based on REQ/ACK timing
- **Offset**: Number of outstanding REQ/ACK cycles allowed

#### Negotiation Flow
```
Driver (SYNC_NEGOCIATE)
    ↓
Send SDTR message
    ↓
Driver (SYNC_SENT_REQUEST) - await response
    ↓
Receive SDTR reply (or timeout)
    ↓
Driver (SYNC_COMPLETED)
    ↓
Use negotiated parameters for all future transfers
```

### 5. Disconnect/Reconnect Mechanism

#### Why Disconnection?
- **Multi-target**: Target may service other hosts while you're busy
- **Fairness**: Prevents one device from monopolizing bus
- **Efficiency**: Other commands can execute while disconnected

#### Disconnect Sequence
1. Device sends DISCONNECT message
2. Driver saves command state to disconnected queue
3. Bus goes to BUS FREE
4. Driver can select different device

#### Reconnection Sequence
1. Device reselects adapter (sends SCSI ID + adapter ID)
2. Driver receives reselection interrupt (SSR = 0x81)
3. Driver identifies which command reconnected
4. Restores saved state (data pointers, transfer count)
5. Resumes data transfer or next phase

### 6. Interrupt-Driven State Machine

#### Interrupt Sources
- **DMA Interrupt**: Terminal count reached (bit 1)
- **SBIC Interrupt**: SCSI phase change or error (bit 3)

#### Re-entrancy Protection
- Only one interrupt can be processed at a time
- Spin loop in `acornscsi_intr()`: while IRQ pending, process
- Prevents race conditions between concurrent interrupts

#### State Storage
- **Ring Buffers**: Capture SSR, phase, IRQ state every interrupt
- **Purpose**: Post-mortem analysis if command hangs or driver crashes
- **Capacity**: 32 entries per device + 1 for global

---

## INTERRUPTS & SYNCHRONIZATION

### Interrupt Flow Architecture

```
Hardware Interrupt (IRQ)
    ↓
ARM CPU: Jump to IRQ handler
    ↓
acornscsi_intr() [IRQ disabled]
    ├─ Read iostatus (interrupt source)
    ├─ if (DMA interrupt)
    │   └─ acornscsi_dma_intr()  [No re-entrancy: DMA complete]
    ├─ if (SBIC interrupt)
    │   └─ acornscsi_sbicintr()  [Re-entrancy: loop until idle]
    ├─ if (DMA xfer pending)
    │   └─ acornscsi_dma_xfer()  [Schedule deferred transfer]
    ├─ if (next command ready)
    │   └─ acornscsi_kick()      [Start next command]
    └─ return IRQ_HANDLED

Interrupt Nesting Loop:
─────────────────────
do {
    ret = INTR_IDLE
    
    if (bus interrupt pending)
        ret = acornscsi_sbicintr()
    
    if (ret == INTR_NEXT_COMMAND)
        ret = acornscsi_kick()
        
} while (ret != INTR_IDLE)

Purpose: Handle chained interrupts within single IRQ context
Example: Selection completion immediately followed by message phase
Result: No need for wait-for-interrupt between phases
```

### Synchronization Primitives

#### Local Spinlock
```c
unsigned long flags;

/* Protect against concurrent interrupt + softirq */
local_irq_save(flags)    /* Save IRQ state, disable IRQ */

/* Critical section: modify shared state */
queue_add_cmd_ordered(&host->queues.issue, SCpnt)

local_irq_restore(flags) /* Restore original IRQ state */
```

#### Why Needed
- `acornscsi_queuecmd()` runs from userspace context (interrupts enabled)
- `acornscsi_intr()` runs from IRQ context (interrupts disabled)
- Both modify `host->queues.issue`
- Without protection: race condition if interrupt occurs during queue modify

#### Scope
- Driver does NOT use spinlock for concurrent multiprocessor support
- Only used to prevent interrupt handler interference
- Sufficient for single-CPU ARM systems (common in Acorn)

### Critical Sections

#### Section 1: Command Queuing
```c
/* From acornscsi_queuecmd() */
local_irq_save(flags)
if (!queue_add_cmd_ordered(&host->queues.issue, SCpnt))
    /* queue full error */
if (host->scsi.phase == PHASE_IDLE)
    acornscsi_kick(host)
local_irq_restore(flags)
```

**Why Protected**:
- Modify issue queue structure
- Read phase variable
- Call `acornscsi_kick()` which modifies host state

#### Section 2: Phase & DMA Updates
```c
/* From interrupt handler - already protected by disable_irq() */
host->scsi.phase = PHASE_CONNECTING
host->SCpnt = SCpnt
host->dma.xfer_setup = 0
/* No explicit locking needed - running in IRQ context */
```

---

## DMA OPERATIONS

### uPC71071 Channel 0 DMA Setup for Data Transfer

#### Write Operation (Memory → Device)

```c
void acornscsi_setup_dma_write(AS_Host *host) {
    unsigned int addr, len;
    
    /* Extract transfer parameters */
    addr = virt_to_phys(host->scsi.SCp.ptr);       /* Physical address */
    len = host->scsi.SCp.this_residual;            /* Bytes in segment */
    
    /* Select DMA channel 0 */
    dmac_write(host->dma.io_port, DMAC_CHANNEL, CHANNEL_0);
    
    /* Program 24-bit memory address */
    dmac_write(host->dma.io_port, DMAC_TXADRLO, (addr >>  0) & 0xFF);
    dmac_write(host->dma.io_port, DMAC_TXADRMD, (addr >>  8) & 0xFF);
    dmac_write(host->dma.io_port, DMAC_TXADRHI, (addr >> 16) & 0xFF);
    
    /* Program transfer count */
    dmac_write(host->dma.io_port, DMAC_TXCNTLO, (len >>  0) & 0xFF);
    dmac_write(host->dma.io_port, DMAC_TXCNTHI, (len >>  8) & 0xFF);
    
    /* Set mode: WRITE to device, BLOCK transfer, SINGLE mode */
    dmac_write(host->dma.io_port, DMAC_MODECON,
               MODECON_WRITE |    /* Direction: memory→device */
               MODECON_BLOCK  |   /* Block transfer mode */
               MODECON_SINGLE |   /* Single mode (not cascade) */
               INIT_MODECON);     /* Standard timing */
    
    /* Device control: timing and handshake */
    dmac_write(host->dma.io_port, DMAC_DEVCON0, INIT_DEVCON0);
    dmac_write(host->dma.io_port, DMAC_DEVCON1, INIT_DEVCON1);
    
    /* Unmask channel 0 to enable DMA requests */
    dmac_write(host->dma.io_port, DMAC_MASKREG,
               MASKREG_M1 | MASKREG_M2 | MASKREG_M3);
               /* Leave M0 unmasked; M1,M2,M3 masked */
}
```

#### Read Operation (Device → Memory)

```c
void acornscsi_setup_dma_read(AS_Host *host) {
    /* Same as write, except mode is READ */
    
    dmac_write(host->dma.io_port, DMAC_MODECON,
               MODECON_READ |     /* Direction: device→memory */
               MODECON_BLOCK  |
               MODECON_SINGLE |
               INIT_MODECON);
}
```

### DMA Interrupt Processing

```c
void acornscsi_dma_intr(AS_Host *host) {
    unsigned int status;
    
    /* Read DMA status */
    status = dmac_read(host->dma.io_port, DMAC_STATUS);
    
    if (status & STATUS_TC0) {
        /* Terminal count reached - all bytes transferred */
        
        /* Update statistics */
        host->dma.transferred = (programmed transfer length);
        
        /* Disable DMA channel */
        dmac_write(host->dma.io_port, DMAC_MASKREG, MASK_ON);
        
        /* Mark as complete */
        host->dma.xfer_done = 1;
    }
    
    if (status & STATUS_RQ0) {
        /* DMA request pending? Should not happen with block mode */
    }
}
```

### Scatter-Gather Support

#### Single vs. Multi-Segment Transfers

```c
struct scsi_pointer {
    char *ptr;                    /* Pointer to current segment */
    int this_residual;            /* Bytes remaining in segment */
    struct scatterlist *buffer;   /* Scatter-gather list */
    int buffers_residual;         /* Segments remaining */
    dma_addr_t phase;             /* Current phase */
};
```

#### Multi-Segment Transfer Sequence

```
Transfer Segment 1:
  - DMA addr = buffer[0].dma_address
  - DMA len = buffer[0].length
  - acornscsi_setup_dma_read()
  - Wait for DMA complete interrupt
  - Mark buffer[0] done

Transfer Segment 2:
  - DMA addr = buffer[1].dma_address
  - DMA len = buffer[1].length
  - acornscsi_setup_dma_read()
  - Wait for DMA complete interrupt
  - Mark buffer[1] done

...repeat for each segment...

After all segments:
  - All pages now in memory
  - Data complete for entire command
  - Return status to SCSI subsystem
```

---

## DEBUG CAPABILITIES

### Debug Levels

```c
#define DEBUG_NO_WRITE    1     /* Prevent writes to specific device */
#define DEBUG_QUEUES      2     /* Log queue operations */
#define DEBUG_DMA         4     /* Log DMA setup/completion */
#define DEBUG_ABORT       8     /* Log aborts */
#define DEBUG_DISCON      16    /* Log disconnections */
#define DEBUG_CONNECT     32    /* Log connections */
#define DEBUG_PHASES      64    /* Log phase changes */
#define DEBUG_WRITE       128   /* Log successful writes */
#define DEBUG_LINK        256   /* Log linked commands */
#define DEBUG_MESSAGES    512   /* Log message processing */
#define DEBUG_RESET       1024  /* Log bus resets */
```

### Conditional Debug Macros

```c
/* Debug specific target only */
#ifdef DEBUG_TARGET
#define DBG(cmd, xxx...) \
    if (cmd->device->id == DEBUG_TARGET) { \
        xxx; \
    }
#else
#define DBG(cmd, xxx...) xxx
#endif

/* Usage */
DBG(SCpnt, printk("scsi%d.%c: command starting\n",
                   host->host->host_no, '0' + target_id));
```

### Status History Ring Buffers

#### Purpose
- Capture hardware state at every interrupt
- Circular 32-entry buffer per device + 1 global
- Useful for post-mortem if command hangs

#### Structure
```c
struct status_entry {
    unsigned long when;           /* jiffies timestamp */
    unsigned char ssr;            /* SCSI Status Register */
    unsigned char ph;             /* Phase at interrupt */
    unsigned char irq;            /* In-IRQ flag */
    unsigned char unused;         /* Padding */
};

#define ADD_STATUS(_q, _ssr, _ph, _irq) \
({ \
    host->status[_q][host->status_ptr[_q]].when = jiffies; \
    host->status[_q][host->status_ptr[_q]].ssr = _ssr; \
    host->status[_q][host->status_ptr[_q]].ph = _ph; \
    host->status[_q][host->status_ptr[_q]].irq = _irq; \
    host->status_ptr[_q] = (host->status_ptr[_q] + 1) \
        & (STATUS_BUFFER_SIZE - 1); \
})
```

### SCSI Interrupt Code Decoding

```c
/* Decode SSR value into human-readable phase string */
static char *acornscsi_interrupttype[] = {
    "rst", "suc", "p/a", "3",
    "term", "5", "6", "7",
    "serv", "9", "a", "b",
    "c", "d", "e", "f"
};

static char *acornscsi_interruptcode[] = {
    /* Phase transitions */
    "reset - normal mode",
    "reset - advanced mode",
    "sel",              /* Selection successful */
    "sel+xfer",         /* Selection + transfer */
    "data-out",         /* Data to device */
    "data-in",          /* Data from device */
    "cmd",              /* Command phase */
    "stat",             /* Status phase */
    ...more codes...
};

/* Example: Print SSR 0x11 */
SSR = 0x11
print_scsi_status(0x11)
  → type = (0x11 >> 4) = 1 → "suc"
  → code = acornscsi_map[0x11] → 2
  → print = "suc:sel" (Selection success)
```

### Procfs Interface

```
cat /proc/scsi/acornscsi/0
  Displays:
  - Host adapter info (ID, base address, IRQ)
  - SCSI device revision (reports "2 TAG" for tagged queueing support)
  - Statistics:
    * Queues: commands queued
    * Removes: commands started
    * Fins: commands completed
    * Reads, Writes, Miscs: command type counts
    * Disconnects: device disconnections
    * Aborts: abort commands issued
    * Resets: bus resets performed
  - Per-device sync transfer parameters
```

---

## ADVANCED TOPICS

### Synchronous Transfer Negotiation (SDTR)

#### Initial Negotiation

```c
void acornscsi_buildmessages(AS_Host *host) {
    /* ... build IDENTIFY message ... */
    
    /* Check if we should negotiate sync transfer */
    if (host->device[target].sync_state == SYNC_NEGOCIATE) {
        /* First time seeing this device */
        host->device[target].sync_state = SYNC_SENT_REQUEST;
        
        /* Build SDTR request message */
        msgqueue_add_byte(&host->scsi.msgs, 0x01);   /* Extended msg */
        msgqueue_add_byte(&host->scsi.msgs, 0x03);   /* SDTR msg len */
        msgqueue_add_byte(&host->scsi.msgs, 0x01);   /* SDTR msg code */
        msgqueue_add_byte(&host->scsi.msgs, 125);    /* Period (ns) */
        msgqueue_add_byte(&host->scsi.msgs, 12);     /* Offset */
    }
}
```

#### Target Response

```
Target can:
1. Accept parameters: Return SDTR with same/better values
2. Reject: Return SDTR with larger period (slower)
3. Ignore: Continue asynchronously (no SDTR reply)
```

#### REQ/ACK Timing

```
Synchronous Transfer Example (Period = 125ns):

Request Phase:        ACK Phase:
────────────          ──────────
T0: REQ ↓            
    Data valid        T0+period: ACK ↓
                      
T0+period: ACK ready
    
T0+period: ACK ↑
    
T0+2×period: REQ ↑

T0+2×period: Ready for next byte

Result: Each byte takes 2×period (250ns = 4 MB/s at maximum)
With offset=12: Can have 12 outstanding REQ/ACK cycles (pipelining)
```

### Reconnection & Reselection

#### Reselection Sequence

```
Device Reselects (claims bus):
  1. Device asserts SEL on SCSI bus
  2. Device places its ID + adapter ID on data lines
  3. Device asserts ATN (for identification)
  4. Adapter detects SEL, reads data lines
  5. WD33C93A generates interrupt: SSR = 0x81 (reselected)

Adapter Response:
  1. Read device ID from SCSI_DATA register
  2. Look up which command was from that device
  3. Retrieve saved state from disconnected queue
  4. Restore data pointers & transfer count
  5. Continue with MESSAGE IN phase
  6. Expect device to send IDENTIFY message
```

#### State Restoration on Reselect

```c
void acornscsi_reconnect(AS_Host *host) {
    unsigned char target, lun;
    
    /* Read reselection ID from SBIC */
    sbic_arm_read(host->scsi.io_port, SBIC_SOURCEID);
    
    /* Extract target and LUN */
    target = extracted_target_id;
    lun = extracted_lun;
    
    /* Save current command (being disconnected) */
    host->origSCpnt = host->SCpnt;
    
    /* Find disconnected command from this device */
    host->SCpnt = find_disconnected_cmd(host, target, lun);
    
    /* Restore data pointer */
    host->scsi.SCp = host->SCpnt->SCp;
    
    /* Restore DMA state */
    host->dma.transferred = saved_transfer_count;
    host->dma.xfer_setup = 0;
    
    /* Update phase */
    host->scsi.phase = PHASE_RECONNECTED;
}
```

### Abort Command Handling

#### Abort Sequence

```c
void acornscsi_abortcmd(AS_Host *host, unsigned char tag) {
    /* Issue ABORT message to device */
    msgqueue_add_byte(&host->scsi.msgs, 0x06);  /* ABORT */
    
    /* Send message */
    acornscsi_sendmessage(host);
    
    /* Device will disconnect after seeing ABORT */
    /* Mark command as aborted */
    host->stats.aborts++;
}
```

#### Error Condition Abort

```
Triggers:
  1. Command timeout (no SCSI activity)
  2. Unexpected disconnect without valid message
  3. Parity error on SCSI bus (PE flag set)
  4. Invalid phase/status combination
  5. User requests abort via /proc

Action:
  1. Issue ABORT command via message queue
  2. Set phase to PHASE_ABORTED
  3. Prepare to complete command with error status
```

### Bus Reset Handling

#### Full Bus Reset

```c
void acornscsi_resetcard(AS_Host *host) {
    /* 1. Assert SCSI reset line */
    host->card.page_reg = 0x80;
    outb(host->card.page_reg, host->card.io_page);
    
    /* 2. Wait 3 centiseconds (SCSI std: 25ms) */
    acornscsi_csdelay(3);
    
    /* 3. De-assert reset */
    host->card.page_reg = 0;
    outb(host->card.page_reg, host->card.io_page);
    
    /* 4. Wait for all devices to reset */
    acornscsi_csdelay(25);
    
    /* 5. Reinitialize SBIC */
    sbic_arm_write(host->scsi.io_port, SBIC_OWNID,
                   OWNID_EAF | adapter_id);
    sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_RESET);
    
    /* 6. Reinitialize DMAC */
    dmac_write(host->dma.io_port, DMAC_INIT, INIT_8BIT);
    
    /* 7. Clear all pending commands */
    host->scsi.phase = PHASE_IDLE;
    host->SCpnt = NULL;
    memset(host->busyluns, 0, sizeof(host->busyluns));
}
```

---

## CONFIGURATION & TUNING

### Key Configuration Parameters

```c
/* Timeouts */
#define TIMEOUT_TIME 10         /* 10 * 10ms = 100ms select timeout */

/* Synchronous Transfer */
#define SDTR_SIZE   12          /* Max 12 bytes in-flight */
#define SDTR_PERIOD 125         /* 125ns = fastest supported */
#define DEFAULT_PERIOD 500      /* 500ns default (safer) */

/* Write Protection */
#define NO_WRITE 0xFE           /* Prevent writes to all but device 0 */

/* Debug Output */
#define DEBUG (DEBUG_RESET|DEBUG_WRITE|DEBUG_NO_WRITE)

/* DMA Configuration */
#define USE_DMAC                /* Must use DMA (PIO not supported) */
```

### Performance Tuning

#### Synchronous vs. Asynchronous
```
Asynchronous (default: 500ns period):
  - Slower but more compatible
  - Safe for older/problematic devices
  - ~2 MB/s typical

Synchronous (125ns period):
  - Faster, requires negotiation
  - ~10 MB/s typical
  - Some devices don't support
```

#### Command Queue Depth
```
Issue Queue: Holds up to N pending commands
  - Longer queue: Better throughput for heavy workloads
  - Shorter queue: Lower latency for interactive I/O
  
Disconnected Queue: Holds N disconnected commands
  - Allows device multitasking
  - Prevents single-device stalls
```

#### DMA Block Size
```
Larger blocks:
  - Fewer interrupts
  - Lower CPU overhead
  - Higher bus efficiency

Smaller blocks:
  - Faster interrupt processing
  - Lower latency
  - Better for slow devices
```

---

## SUMMARY: Key Takeaways

1. **State Machine Architecture**: Driver uses interrupt-driven state machine, not blocking waits
2. **Hardware Abstraction**: Cleanly separates WD33C93A and uPC71071 controller interfaces
3. **Performance**: DMA-based data transfer minimizes CPU overhead during I/O
4. **Flexibility**: Supports SCSI advanced features (disconnect/reconnect, SDTR negotiation, tagged queueing)
5. **Reliability**: Extensive error checking, timeout handling, and debug infrastructure
6. **Portability**: Clean separation from ARM-specific code (mostly in asm/ecard.h)

---

**Document Version**: 1.0
**Linux Kernel**: 2.6.20
**Architecture**: ARM (Acorn RPC/CL7500)
**Last Updated**: 2024

