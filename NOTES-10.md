# ASCII Diagram — IDT vs irq_desc
──────────────────────────────────────────────────────────────────────────────
HARDWARE LEVEL (CPU)
──────────────────────────────────────────────────────────────────────────────
      ┌────────────────────────────┐
IDT → │ Entry[32] = Timer Gate     │──┐
      │ Entry[33] = Keyboard Gate  │  │   CPU fetches entry by vector number
      │ Entry[14] = Page Fault     │  │
      └────────────────────────────┘  │
               │                      │
               ▼                      │
     vector = 32 (Timer IRQ0)         │
               │                      │
               ▼                      │
      Assembly stub: irq0_interrupt() │
               │                      │
               ▼                      │
──────────────────────────────────────────────────────────────────────────────
SOFTWARE LEVEL (Linux kernel)
──────────────────────────────────────────────────────────────────────────────
      do_IRQ(0)
         │
         ▼
      irq_desc[0] ────────────────────────────────────────────────┐
         │ status=...                                             │
         │ handler = i8259A_irq_type (chip ops)                   │
         │ action = timer_interrupt_handler (from request_irq())  │
         │ lock = spinlock_t                                      │
         └────────────────────────────────────────────────────────┘
               │
               ▼
        timer_interrupt()
            └─► update_process_times()
            └─► run_local_timers()
            └─► scheduler_tick()
──────────────────────────────────────────────────────────────────────────────



# Example Flow: Timer Interrupt (IRQ0)
──────────────────────────────────────────────────────────────────────────────
CPU HARDWARE FLOW
──────────────────────────────────────────────────────────────────────────────
(1) PIT sends IRQ#0 → APIC routes → CPU receives vector 32
(2) CPU:
     vector = 32
     IDT[32] = interrupt gate → address = irq0_interrupt
(3) CPU jumps to irq0_interrupt (in entry_64.S)
     → saves context
     → calls do_IRQ(0)
──────────────────────────────────────────────────────────────────────────────
KERNEL SOFTWARE FLOW
──────────────────────────────────────────────────────────────────────────────
do_IRQ(irq=0)
   └─► desc = irq_desc[0]
   └─► desc->handler->ack(irq)
   └─► action = desc->action
           ├─► action->handler(irq, dev_id)
           └─► action->next (chained handlers)
──────────────────────────────────────────────────────────────────────────────



# how IDT & irq_desc connetcs 
CPU → IDT entry → low-level assembly stub (common_interrupt)
              → do_IRQ() (C)
                    → irq_desc[irq]
                          → desc->handler->ack()
                          → run list of desc->action handlers



#   IDT vs irq_desc
| Table                                                   | Owned by     | Describes                                                               | Triggered by              | Used by         |
| ------------------------------------------------------- | ------------ | ----------------------------------------------------------------------- | ------------------------- | --------------- |
| **IDT (Interrupt Descriptor Table)**                    | CPU hardware | *How to enter kernel code* when an interrupt/trap happens               | CPU core (hardware)       | Processor       |
| **irq_desc[] (Interrupt Descriptor Table in software)** | Linux kernel | *How to handle and route* a given hardware interrupt once in the kernel | Linux interrupt subsystem | Kernel software |



# Complete Flow: From Hardware IRQ → IDT → irq_desc → SoftIRQ → User Space
══════════════════════════════════════════════════════════════════════════════
                         HARDWARE & CPU ENTRY (TOP LEVEL)
══════════════════════════════════════════════════════════════════════════════
[DEVICE LEVEL]
──────────────
Network card (e1000) asserts interrupt line → APIC routes signal to CPU

[CPU LEVEL: VECTOR DISPATCH]
──────────────
      CPU receives interrupt vector = 0x20 + irq_line (e.g., 0x2B)
          │
          ▼
      IDT[0x2B] entry → interrupt gate
          ├─ selector: __KERNEL_CS
          ├─ offset:  irq_common_entry (assembly stub)
          └─ type:    interrupt gate (clears IF flag)
          │
          ▼
──────────────────────────────────────────────────────────────────────────────
ENTRY_64.S HANDLER
──────────────────────────────────────────────────────────────────────────────
irq_common_entry:
    push regs
    call do_IRQ(vector - FIRST_EXTERNAL_VECTOR)   ← software layer entry

══════════════════════════════════════════════════════════════════════════════
                             KERNEL SOFTWARE DISPATCH
══════════════════════════════════════════════════════════════════════════════
do_IRQ(irq = 11)
   │
   ├─► desc = irq_desc[11]           ← software IRQ descriptor table
   │       ├─ handler = apic_chip_ops (mask/ack/eoi)
   │       └─ action = e1000_intr()  ← registered by driver (request_irq)
   │
   ├─► desc->handler->ack(irq)
   ├─► call desc->action->handler(irq, dev_id)
   │        │
   │        └─► e1000_intr()
   │               ├─ read NIC status registers
   │               ├─ enqueue received packets into NAPI queue
   │               └─ raise_softirq_irqoff(NET_RX_SOFTIRQ)
   │
   └─► irq_exit()
           if (!in_interrupt() && local_softirq_pending())
               invoke_softirq()   ←── triggers softirq execution

══════════════════════════════════════════════════════════════════════════════
                                 SOFTIRQ DISPATCH
══════════════════════════════════════════════════════════════════════════════
do_softirq()
   └─► __do_softirq()
           set_softirq_pending(0)
           local_irq_enable()
           iterate over softirq_vec[32]:
               if bit == NET_RX_SOFTIRQ:
                     softirq_vec[NET_RX_SOFTIRQ].action → net_rx_action()

══════════════════════════════════════════════════════════════════════════════
                     NETWORK RECEIVE SOFTIRQ HANDLER (NET_RX_SOFTIRQ)
══════════════════════════════════════════════════════════════════════════════
net_rx_action()
   ├─ loop over NAPI structures (one per network device)
   │     if work pending:
   │         napi->poll()
   │             ├─ e1000_clean_rx_irq()
   │             │     - pull packets from RX ring
   │             │     - build sk_buff (skb)
   │             │     - call netif_receive_skb(skb)
   │             │
   │             └─ may requeue if incomplete
   │
   └─ if backlog not cleared:
           raise_softirq_irqoff(NET_RX_SOFTIRQ)
           wakeup_softirqd()   ← wake ksoftirqd/N for deferred processing

══════════════════════════════════════════════════════════════════════════════
                             PER-CPU KSOFTIRQD THREAD
══════════════════════════════════════════════════════════════════════════════
ksoftirqd/N (spawned by spawn_ksoftirqd())
   │
   ├─ while (!kthread_should_stop()):
   │      if (!local_softirq_pending())
   │           schedule();     // sleep until woken by wakeup_softirqd()
   │
   │      while (local_softirq_pending()):
   │           do_softirq();   // run pending softirqs in process context
   │
   └─ runs in process context → can yield CPU (avoids softirq livelock)

══════════════════════════════════════════════════════════════════════════════
                                  SOCKET DELIVERY
══════════════════════════════════════════════════════════════════════════════
netif_receive_skb()
   └─► ip_rcv()
          └─► tcp_v4_rcv()
                 └─► tcp_data_queue()
                        └─► wake_up(sk->sleep)
                               ↑
                               │ wakes process blocked in
                               │ recvmsg() / select() / poll()
                               │
══════════════════════════════════════════════════════════════════════════════
                                   USER SPACE
══════════════════════════════════════════════════════════════════════════════
Application process (e.g., sshd)
   │
   └─► recvfrom(sock, buf, len)
           blocks in schedule()
           awakened by wake_up()
           copies skb payload from kernel socket buffer → user buffer



# Zoom-IN
══════════════════════════════════════════════════════════════════════════════
                         HARDWARE & CPU ENTRY (TOP LEVEL)
══════════════════════════════════════════════════════════════════════════════

[1] DEVICE LEVEL
──────────────────────────────────────────────────────────────────────────────
   Device (e.g., e1000 NIC) asserts interrupt line IRQ11
        │
        ▼
   APIC / IO-APIC routes signal → CPU receives vector = 0x20 + 11 (0x2B)
        │
        ▼
──────────────────────────────────────────────────────────────────────────────
[2] CPU VECTOR DISPATCH (IDT)
──────────────────────────────────────────────────────────────────────────────
      IDT[0x2B] entry  ← set by set_intr_gate(0x20 + irq, interruptX)
        │
        ├─ descriptor fields:
        │    - Selector:  __KERNEL_CS
        │    - Offset:    irq_common_entry (assembly stub)
        │    - Type:      Interrupt gate (clears IF)
        │
        ▼
      CPU jumps to assembly stub → irq_common_entry (entry_64.S)
        │
        ├─ saves registers
        ├─ builds pt_regs
        └─ calls:  do_IRQ(vector - FIRST_EXTERNAL_VECTOR)
                        e.g. do_IRQ(11)
══════════════════════════════════════════════════════════════════════════════
                           KERNEL INTERRUPT CORE
══════════════════════════════════════════════════════════════════════════════

do_IRQ(irq = 11)
   │
   ├─► desc = &irq_desc[11]       ← per-IRQ descriptor from <linux/irq.h>
   │
   │   struct irq_desc {
   │       irq_flow_handler_t handle_irq;   // high-level flow fn
   │       struct irq_chip *chip;           // low-level hw ops
   │       struct irqaction *action;        // driver list
   │       unsigned int status;             // IRQ_INPROGRESS, etc.
   │       spinlock_t lock;
   │       ...
   │   };
   │
   ├─► desc->handle_irq(irq, desc)
   │        (usually one of: handle_edge_irq, handle_level_irq, ...)
   │
   │        ↓
   │        ├─► desc->chip->ack(irq)      (mask/ack at hardware level)
   │        ├─► handle_IRQ_event(irq, desc->action)
   │        │         ↓
   │        │         ├─ iterate through action chain:
   │        │         │     action->handler(irq, dev_id)
   │        │         │     action->next ...
   │        │         └─ e.g. e1000_intr()
   │        │               ├─ reads device status registers
   │        │               ├─ queues received packets
   │        │               └─ raise_softirq_irqoff(NET_RX_SOFTIRQ)
   │        │
   │        └─► desc->chip->eoi(irq)  (End Of Interrupt)
   │
   └─► irq_exit()
          if (!in_interrupt() && local_softirq_pending())
              invoke_softirq()   ← start bottom-half processing
══════════════════════════════════════════════════════════════════════════════
                             SOFTIRQ DISPATCH
══════════════════════════════════════════════════════════════════════════════
do_softirq()
   └─► __do_softirq()
          │
          ├─ set_softirq_pending(0)
          ├─ local_irq_enable()
          └─ iterate over softirq_vec[32]:
                 if (bit == NET_RX_SOFTIRQ)
                      net_rx_action()   ← registered in softirq_init()

══════════════════════════════════════════════════════════════════════════════
                     NETWORK RECEIVE SOFTIRQ HANDLER
══════════════════════════════════════════════════════════════════════════════
net_rx_action()
   ├─ for each napi struct in backlog:
   │     napi->poll()
   │         ├─ driver poll fn (e1000_clean_rx_irq)
   │         │      - pulls packets from RX ring
   │         │      - builds skb
   │         │      - calls netif_receive_skb(skb)
   │         └─ may requeue if incomplete
   │
   └─ if more work pending:
           raise_softirq_irqoff(NET_RX_SOFTIRQ)
           wakeup_softirqd()   ← wake ksoftirqd/N

══════════════════════════════════════════════════════════════════════════════
                          KSOFTIRQD THREAD (PER CPU)
══════════════════════════════════════════════════════════════════════════════
ksoftirqd/<cpuN>
   │
   ├─ created by spawn_ksoftirqd() at boot
   │
   ├─ while (!kthread_should_stop()):
   │       if (!local_softirq_pending())
   │             schedule();         // sleep
   │       while (local_softirq_pending())
   │             do_softirq();       // process deferred work
   │
   └─ runs in process context (can reschedule/yield)

══════════════════════════════════════════════════════════════════════════════
                            PROTOCOL STACK DELIVERY
══════════════════════════════════════════════════════════════════════════════
netif_receive_skb()
   └─► ip_rcv()
          └─► tcp_v4_rcv()
                 └─► tcp_data_queue()
                        └─► wake_up(sk->sleep)
                               │
                               └─ wakes process blocked in recvmsg()

══════════════════════════════════════════════════════════════════════════════
                                   USER SPACE
══════════════════════════════════════════════════════════════════════════════
Application process (e.g., sshd)
   │
   └─► recvfrom(sock, buf, len)
          blocks in schedule()
          awakened by wake_up()
          copies skb payload → user buffer

