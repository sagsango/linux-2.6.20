# soft irqs
 - Softirqs are purely software-generated interrupts — never triggered directly by hardware.
 - These are also called bottom half or deffered work

[Hardware Interrupt: NIC, Disk, Timer]
           │
           ▼
       Top Half (ISR)
           │
           ├── Acknowledge device
           ├── Queue data or event
           └── raise_softirq(SOFTIRQ_TYPE)
                    │
                    ▼
             __do_softirq()  (later)
                    │
                    └── softirq_vec[type].action()


Hardware (NIC) ───► IRQ
                     │
                     ▼
             e1000_intr()             ← top half
                ├─ Acknowledge IRQ
                ├─ Queue SKB
                └─ raise_softirq(NET_RX_SOFTIRQ)
                     │
                     ▼
             __do_softirq()
                └─ net_rx_action()
                     └─ netif_receive_skb()
                          └─ IP/TCP processing



# All aviable soft irqs
+-------------------+---------------------------+---------------------------+-------------------------------+
| SoftIRQ Name      | Function / Handler        | Typical Source            | Purpose                       |
+-------------------+---------------------------+---------------------------+-------------------------------+
| HI_SOFTIRQ        | hi_tasklet_action()       | tasklet_hi_schedule()     | Critical deferred tasklets (hi priority)  |
| TIMER_SOFTIRQ     | run_timer_softirq()       | Expired kernel timers     | Run timer callbacks            |
| NET_TX_SOFTIRQ    | net_tx_action()           | TX completion             | Finish packet sends            |
| NET_RX_SOFTIRQ    | net_rx_action()           | NIC IRQ, packet input     | Process received packets       |
| BLOCK_SOFTIRQ     | blk_softirq_action()      | Disk IRQs                 | Finish block I/O requests      |
| TASKLET_SOFTIRQ   | tasklet_action()          | tasklet_schedule()        | Normal deferred tasklets       |
| SCHED_SOFTIRQ     | run_rebalance_domains()   | Timer tick                | Scheduler load balancing       |
+-------------------+---------------------------+---------------------------+-------------------------------+


# how soft irqs get triggered
═══════════════════════════════════════════════════════════════════════════
                    LINUX 2.6.20 — SOFTIRQ EXECUTION FLOW
═══════════════════════════════════════════════════════════════════════════

                 [ HARDWARE INTERRUPT OCCURS ]
                               │
                               ▼
───────────────────────────────────────────────────────────────────────────
1️⃣  CPU traps into interrupt descriptor (IDT entry)
───────────────────────────────────────────────────────────────────────────
IDT[IRQ#] → common_interrupt()
                │
                ▼
             do_IRQ(irq)
                │
                ▼
        handle_level_irq() / handle_edge_irq()
                │
                ▼
        → Calls the registered IRQ handler (Top Half)
───────────────────────────────────────────────────────────────────────────
2️⃣  TOP HALF (HARD IRQ CONTEXT)
───────────────────────────────────────────────────────────────────────────
Driver ISR example:  e1000_intr(), ide_intr(), timer_interrupt(), etc.
                │
                ├── Acknowledge hardware interrupt (read status register)
                ├── Queue or record work to be done later
                └── Raise a softirq:

                     raise_softirq(SOFTIRQ_TYPE)
                              │
                              ▼
                     __raise_softirq_irqoff()
                              │
                              ▼
                     marks bit in per-CPU vector:
                          local_softirq_pending() |= (1 << SOFTIRQ_TYPE)
───────────────────────────────────────────────────────────────────────────
3️⃣  EXIT FROM HARD IRQ — Check pending softirqs
───────────────────────────────────────────────────────────────────────────
irq_exit()
   │
   ├──> if (local_softirq_pending())
   │         └─► invoke __do_softirq()
   │
   └──> return from interrupt
───────────────────────────────────────────────────────────────────────────
4️⃣  __do_softirq()  [kernel/softirq.c]
───────────────────────────────────────────────────────────────────────────
__do_softirq():
   │
   ├── pending = local_softirq_pending()
   │
   ├── Clear pending bits
   │
   ├── For each bit set in 'pending':
   │         softirq_vec[i].action()   ← function pointer table
   │
   └── If time budget exceeded → ksoftirqd kernel thread runs them later
───────────────────────────────────────────────────────────────────────────
5️⃣  SOFTIRQ HANDLERS DISPATCH TABLE
───────────────────────────────────────────────────────────────────────────
softirq_vec[] =
{
   [HI_SOFTIRQ]     = hi_tasklet_action,
   [TIMER_SOFTIRQ]  = run_timer_softirq,
   [NET_TX_SOFTIRQ] = net_tx_action,
   [NET_RX_SOFTIRQ] = net_rx_action,
   [BLOCK_SOFTIRQ]  = blk_softirq_action,
   [TASKLET_SOFTIRQ]= tasklet_action,
   [SCHED_SOFTIRQ]  = run_rebalance_domains,
}
───────────────────────────────────────────────────────────────────────────
6️⃣  INDIVIDUAL SOFTIRQ FLOWS
───────────────────────────────────────────────────────────────────────────
                     ┌──────────────────────────┐
                     │   HI_SOFTIRQ (0)         │
                     │   hi_tasklet_action()    │
                     │   → Runs high-prio tasklets
                     └──────────────────────────┘
                               │
                               ▼
                     ┌──────────────────────────┐
                     │   TIMER_SOFTIRQ (1)      │
                     │   run_timer_softirq()    │
                     │   → Executes expired timers
                     └──────────────────────────┘
                               │
                               ▼
                     ┌──────────────────────────┐
                     │   NET_TX_SOFTIRQ (2)     │
                     │   net_tx_action()        │
                     │   → Frees TX skbs, completes send
                     └──────────────────────────┘
                               │
                               ▼
                     ┌──────────────────────────┐
                     │   NET_RX_SOFTIRQ (3)     │
                     │   net_rx_action()        │
                     │   → Process received packets
                     └──────────────────────────┘
                               │
                               ▼
                     ┌──────────────────────────┐
                     │   BLOCK_SOFTIRQ (4)      │
                     │   blk_softirq_action()   │
                     │   → Finish disk I/O requests
                     └──────────────────────────┘
                               │
                               ▼
                     ┌──────────────────────────┐
                     │   TASKLET_SOFTIRQ (5)    │
                     │   tasklet_action()       │
                     │   → Run normal-prio tasklets
                     └──────────────────────────┘
                               │
                               ▼
                     ┌──────────────────────────┐
                     │   SCHED_SOFTIRQ (6)      │
                     │   run_rebalance_domains()│
                     │   → Load balancing, scheduling tasks
                     └──────────────────────────┘
───────────────────────────────────────────────────────────────────────────
7️⃣  COMPLETION PATH
───────────────────────────────────────────────────────────────────────────
After all active softirqs handled:
   │
   ├── If new ones raised during handling → loop again
   │
   └── Exit back to interrupted context (process or idle)
───────────────────────────────────────────────────────────────────────────
8️⃣  FALLBACK THREAD CONTEXT
───────────────────────────────────────────────────────────────────────────
If softirqs pending but cannot run in interrupt context (e.g., CPU time limit):
   │
   └── kernel thread per CPU runs them later:
        ksoftirqd/<cpu-id>
           └─> run_ksoftirqd()
                 └─> __do_softirq()
───────────────────────────────────────────────────────────────────────────

