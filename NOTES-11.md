# see these pdf
    irq-ide.pdf
    irq-net.pdf
# Bottom half example
═══════════════════════════════════════════════════════════════════════════
                    HARDWARE AND TOP-HALF FLOW
═══════════════════════════════════════════════════════════════════════════

        +---------------------------------------------------+
        |                 Hardware (IDE Disk)               |
        |---------------------------------------------------|
        |  DMA or PIO transfer completes → assert IRQ14/15  |
        +---------------------------------------------------+
                              │
                              ▼
        +---------------------------------------------------+
        |           CPU interrupt entry via IDT             |
        |---------------------------------------------------|
        |  vector = 0x2E (IRQ14) or 0x2F (IRQ15)            |
        |  → common_interrupt → do_IRQ(irq)                 |
        +---------------------------------------------------+
                              │
                              ▼
        +---------------------------------------------------+
        |             irq_desc[irq].action.handler           |
        |                 = ide_intr()                       |
        +---------------------------------------------------+
                              │
                              ▼
═══════════════════════════════════════════════════════════════════════════
                       ide_intr(): “combined top+bottom half”
═══════════════════════════════════════════════════════════════════════════

ide_intr(irq, hwgroup)
│
├─► spin_lock_irqsave(&ide_lock)
│
├─► ide_ack_intr(hwif)                   ← acknowledges the hardware IRQ
│     (clears controller status register)
│
├─► handler = hwgroup->handler           ← this is the function installed
│       (set earlier by ide_execute_command)
│
├─► hwgroup->handler = NULL              ← mark handler consumed
│
├─► del_timer(&hwgroup->timer)           ← stop timeout watchdog
│
└─► spin_unlock_irqrestore(&ide_lock)
      │
      ▼
───────────────────────────────────────────────────────────────────────────
                 ▼▼▼▼▼▼  BOTTOM HALF EXECUTION  ▼▼▼▼▼▼
───────────────────────────────────────────────────────────────────────────
      (executed directly inside ide_intr() instead of via tasklet)
      │
      ▼
      startstop = handler(drive)
           │
           ├─ Example handlers:
           │     • read_intr()        → read data from IDE_DATA_REG
           │     • dma_intr()         → finish DMA, check errors
           │     • task_no_data_intr()→ handle non-data commands
           │
           ├─ Each handler typically:
           │     • Calls ide_end_request() to finish I/O
           │     • Possibly schedules next command
           │     • Returns ide_stopped when done
           │
           └─ Thus this handler *is* the deferred processing:
           │      → executes after hardware IRQ acknowledgment
           │      → performs “bottom-half” logic directly
           ▼
───────────────────────────────────────────────────────────────────────────
                     ide_intr() post-handler cleanup
───────────────────────────────────────────────────────────────────────────
spin_lock_irq(&ide_lock)
│
└─► if (startstop == ide_stopped)
        ide_do_request(hwgroup, hwif->irq)
           │
           ▼
           start next request from queue
───────────────────────────────────────────────────────────────────────────

(If no new IRQ arrives within timeout:)
       timer → ide_timer_expiry()
             → calls same handler(drive)
             → ide_do_request(hwgroup, IDE_NO_IRQ)
───────────────────────────────────────────────────────────────────────────


# top half (small)
═════════════════════════════════════════════════════════════════════════
        e1000 INTERRUPT FLOW — Linux 2.6.20 (Pre-NAPI)
═════════════════════════════════════════════════════════════════════════

Hardware (e1000) ───────► IRQ17
       │
       ▼
IDT[vector_for_IRQ17]
   └─► common_interrupt
         └─► do_IRQ(17)
              └─► handle_level_irq()
                   └─► e1000_intr()      ← registered via request_irq()
─────────────────────────────────────────────────────────────────────────
TOP HALF (hard-IRQ)
─────────────────────────────────────────────────────────────────────────
e1000_intr()
   ├─ Read ICR (acknowledge interrupt)
   ├─ Mask further interrupts (IMC)
   ├─ Handle link-status changes (optional)
   ├─ Schedule tasklets:
   │     tasklet_schedule(&adapter->tx_tasklet);
   │     tasklet_schedule(&adapter->rx_tasklet);
   └─ return IRQ_HANDLED
─────────────────────────────────────────────────────────────────────────
irq_exit()
   └─► if (local_softirq_pending())
           do_softirq()
─────────────────────────────────────────────────────────────────────────
BOTTOM HALF (softirq → tasklet)
─────────────────────────────────────────────────────────────────────────
do_softirq()
   └─► softirq_vec[TASKLET_SOFTIRQ] → tasklet_action()
          └─► e1000_clean_tx_irq(adapter)
          │     - reclaim TX descriptors
          │     - free skb buffers
          │
          └─► e1000_clean_rx_irq(adapter)
                - process RX descriptors
                - allocate skb
                - netif_receive_skb()
─────────────────────────────────────────────────────────────────────────
USERSPACE
─────────────────────────────────────────────────────────────────────────
netif_receive_skb()
   └─► ip_rcv() → tcp_v4_rcv()
        └─► wake_up(sock->wait)
             └─► recvfrom() in user process unblocks



# top half (big)
═══════════════════════════════════════════════════════════════════════════════
                  HARDWARE LEVEL
═══════════════════════════════════════════════════════════════════════════════
Ethernet Controller (Intel e1000)
  │
  ├─ Receives a frame from the wire
  │     - DMA engine writes RX descriptor(s) and packet data
  │       into host memory (via PCI bus)
  │     - Updates RX descriptor ring (DD bit set)
  │     - Triggers interrupt (INTA# → APIC)
  │
  └─► [Hardware IRQ 17 asserted]
═══════════════════════════════════════════════════════════════════════════════
                 CPU / IDT / GENERIC IRQ ENTRY
═══════════════════════════════════════════════════════════════════════════════
IDT[0x31] → entry_0x31 (asm)
   │
   └─► common_interrupt()
          └─► do_IRQ(17)
                 └─► handle_level_irq(17)
                        └─► irq_desc[17].action->handler
                                = e1000_intr()
═══════════════════════════════════════════════════════════════════════════════
                 TOP HALF (Hard-IRQ Context)
═══════════════════════════════════════════════════════════════════════════════
e1000_intr(int irq, void *data)
   Context: hard IRQ, interrupts disabled
   Stack:   per-CPU irq stack
   Cannot:  sleep, allocate GFP_KERNEL memory, or block

   ┌──────────────────────────────────────────────────────────────┐
   │ 1. Read Interrupt Cause Register (ICR)                      │
   │      icr = E1000_READ_REG(hw, ICR);                         │
   │      → acknowledges interrupt in hardware                   │
   │      → identifies cause (RX, TX, LSC, etc.)                 │
   │                                                             │
   │ 2. Mask further interrupts                                  │
   │      E1000_WRITE_REG(hw, IMC, ~0);                          │
   │      (prevents interrupt storm during processing)           │
   │                                                             │
   │ 3. Handle special bits (e.g. Link-Status-Change)            │
   │      schedule watchdog timer if needed                      │
   │                                                             │
   │ 4. Schedule deferred work (bottom halves)                   │
   │      tasklet_schedule(&adapter->tx_tasklet);                │
   │      tasklet_schedule(&adapter->rx_tasklet);                │
   │                                                             │
   │ 5. Return IRQ_HANDLED                                       │
   └──────────────────────────────────────────────────────────────┘
   ↓
irq_exit()           [kernel/softirq.c]
   │
   └─► if (local_softirq_pending())
           do_softirq()
═══════════════════════════════════════════════════════════════════════════════
                 SOFTIRQ DISPATCH
═══════════════════════════════════════════════════════════════════════════════
do_softirq()
   └─► for each pending softirq type:
          if bit == TASKLET_SOFTIRQ:
              softirq_vec[TASKLET_SOFTIRQ].action = tasklet_action()

tasklet_action()
   └─► while (tasklet_list not empty)
           take tasklet t = adapter->rx_tasklet or tx_tasklet
           call t->func(t->data)
═══════════════════════════════════════════════════════════════════════════════
                 BOTTOM HALF (Tasklet Context)
═══════════════════════════════════════════════════════════════════════════════
[executed in softirq context, interrupts enabled but atomic]

→ RX tasklet
   e1000_clean_rx_irq(struct e1000_adapter *adapter)
     ├─ Iterate over RX descriptor ring (adapter->rx_ring)
     │     for each descriptor with DD bit set:
     │         skb = dev_alloc_skb()
     │         copy DMA’d packet data into skb->data
     │         skb->protocol = eth_type_trans(skb, netdev)
     │         netif_receive_skb(skb)
     │         reinitialize descriptor (DD=0)
     └─ Update RX tail pointer (hardware register RDT)
        E1000_WRITE_REG(hw, RDT, new_tail)

→ TX tasklet
   e1000_clean_tx_irq(struct e1000_adapter *adapter)
     ├─ Iterate over TX descriptor ring
     │     if descriptor done:
     │         free corresponding skb
     │         update stats
     └─ Wake queue if netif_queue_stopped()
═══════════════════════════════════════════════════════════════════════════════
                 NETWORK STACK (SoftIRQ continues)
═══════════════════════════════════════════════════════════════════════════════
netif_receive_skb(skb)
   └─► net/core/dev.c
        ├─ Determines protocol (skb->protocol)
        ├─ Calls protocol handler:
        │     ETH_P_IP   → ip_rcv()
        │     ETH_P_ARP  → arp_rcv()
        │     ETH_P_IPV6 → ipv6_rcv()
        │     etc.
        │
        └─ Returns once packet enqueued to protocol layer
───────────────────────────────────────────────────────────────────────────────
ip_rcv() → ip_input.c
   └─► Checks IP header, routing, fragmentation
        ↓
     tcp_v4_rcv() / udp_rcv()
        ↓
     tcp_data_queue()
        ↓
     wake_up(sk->sleep)   ← wakes process waiting on socket
═══════════════════════════════════════════════════════════════════════════════
                 USERSPACE
═══════════════════════════════════════════════════════════════════════════════
Application (e.g. sshd, curl, ping)
   └─► recvfrom(sock, buf, len)
         ↓
       sys_recvfrom()
         ↓
       sock_recvmsg()
         ↓
       sk_wait_data() → schedule() if no data
         ↓
       wake_up() from tcp_data_queue() → process runs again
         ↓
       skb_copy_datagram_iovec() → copy packet to user buffer
═══════════════════════════════════════════════════════════════════════════════
                 DATA STRUCTURE LINKS
═══════════════════════════════════════════════════════════════════════════════
┌──────────────────────────────────────────────────────┐
│ struct e1000_adapter                                 │
│ ├─ struct net_device *netdev                         │
│ ├─ struct e1000_hw hw                                │
│ ├─ struct e1000_rx_ring *rx_ring                     │
│ ├─ struct e1000_tx_ring *tx_ring                     │
│ ├─ struct tasklet_struct rx_tasklet                  │
│ ├─ struct tasklet_struct tx_tasklet                  │
│ └─ spinlock_t tx_lock, rx_lock                       │
└──────────────────────────────────────────────────────┘
        │
        ▼
┌──────────────────────────────────────────────────────┐
│ struct tasklet_struct                                │
│ ├─ void (*func)(unsigned long) → e1000_clean_rx_irq  │
│ ├─ unsigned long data → (unsigned long)adapter       │
│ └─ struct list_head entry                            │
└──────────────────────────────────────────────────────┘
        │
        ▼
softirq_vec[TASKLET_SOFTIRQ] → tasklet_action()
═══════════════════════════════════════════════════════════════════════════════
                 TIMING CHARACTERISTICS
═══════════════════════════════════════════════════════════════════════════════
Context                | Interrupts | Preemptible | May Sleep | Purpose
────────────────────────┼────────────┼──────────────┼───────────┼──────────────
Top half (e1000_intr)   | Disabled   | No           | No        | Quick ack, schedule work
Bottom half (tasklet)   | Enabled    | No           | No        | Heavy RX/TX cleanup
Process context         | Enabled    | Yes          | Yes       | User copy, sockets
═══════════════════════════════════════════════════════════════════════════════
