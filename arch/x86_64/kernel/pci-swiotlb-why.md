```text
============================================================
SWIOTLB AND BOUNCE BUFFERS
Complete Background, Motivation, Design, and Flow
============================================================

This document explains:

    • Why bounce buffers exist
    • Why normal DMA sometimes fails
    • How SWIOTLB works
    • Why copies are required
    • Why bounce buffers are reused
    • How SWIOTLB differs from a real IOMMU

If you understand this file, you will understand
most DMA issues encountered in Linux kernel work.

============================================================
CHAPTER 1
THE ORIGINAL DMA MODEL
============================================================

Let's start from the beginning.

A device wants to read or write RAM.

Without DMA:

CPU copies everything.

============================================================

Disk
  |
  v
CPU
  |
  v
RAM

============================================================

This wastes CPU cycles.

DMA was invented so devices can access RAM directly.

============================================================

Disk
  |
  v
RAM

CPU not involved in data transfer.

============================================================

This is much faster.

============================================================
CHAPTER 2
WHAT ADDRESS DOES A DEVICE USE?
============================================================

Many beginners think:

    Device uses virtual address.

Wrong.

============================================================

CPU uses:

Virtual Address
      |
      v
Page Tables
      |
      v
Physical Address

============================================================

Device uses:

DMA Address
      |
      v
Memory

============================================================

Historically:

DMA Address == Physical Address

============================================================

Example:

Physical RAM

0x20000000

============================================================

Driver:

dma_map_single()

============================================================

Kernel returns:

DMA Address = 0x20000000

============================================================

Device performs DMA:

0x20000000

============================================================

Everything works.

============================================================
CHAPTER 3
WHY THIS BREAKS ON 64-BIT SYSTEMS
============================================================

Old PCI devices often support only:

32-bit DMA

============================================================

Maximum address:

0xffffffff

= 4GB

============================================================

Now imagine:

Machine RAM = 64GB

============================================================

Physical Memory Layout

0GB -------------------- 4GB -------------------- 64GB

Device can reach:
^^^^^^^^^^^^^^^^^^^^^^^

Cannot reach:
                        ^^^^^^^^^^^^^^^^^^^^^^^^

============================================================

Driver allocates buffer:

0x180000000

(6GB)

============================================================

Driver:

dma_map_single()

============================================================

Kernel gets:

0x180000000

============================================================

Device sees:

"Sorry, I only understand 32-bit addresses."

============================================================

DMA cannot happen.

============================================================
CHAPTER 4
POSSIBLE SOLUTIONS
============================================================

There are three possible solutions.

============================================================
Solution #1
Allocate Low Memory
============================================================

Allocate everything below 4GB.

Works.

But eventually low memory runs out.

Not scalable.

============================================================
Solution #2
Hardware IOMMU
============================================================

Device DMA Address
       |
       v
IOMMU
       |
       v
High Physical Memory

No copies needed.

Modern solution.

Examples:

    Intel VT-d
    AMD-Vi
    ARM SMMU
    Calgary
    GART

============================================================
Solution #3
SWIOTLB
============================================================

No hardware IOMMU.

Use software bounce buffers.

This is what we're studying.

============================================================
CHAPTER 5
WHAT IS A BOUNCE BUFFER?
============================================================

A bounce buffer is:

    A temporary low-memory buffer that the
    device CAN access.

============================================================

Real Buffer

0x180000000

============================================================

Bounce Buffer

0x01000000

============================================================

Device can access:

0x01000000

============================================================

Device cannot access:

0x180000000

============================================================

So Linux creates a translator.

============================================================
CHAPTER 6
THE SWIOTLB POOL
============================================================

At boot Linux reserves a large low-memory area.

============================================================

SWIOTLB Pool

+----------------------------------+
| Slot 0                           |
| Slot 1                           |
| Slot 2                           |
| Slot 3                           |
| Slot 4                           |
| Slot 5                           |
| ...                              |
+----------------------------------+

============================================================

Historically:

Each slot = 2KB

============================================================

Pool might contain thousands of slots.

============================================================
CHAPTER 7
DMA_TO_DEVICE
============================================================

Example:

Network transmit

Disk write

GPU upload

============================================================

Driver Buffer

HELLO

Located at:

0x180000000

============================================================

Device cannot reach it.

============================================================

Step 1

Allocate bounce buffer.

============================================================

Bounce Buffer

0x01000000

============================================================

Step 2

Copy data.

============================================================

Real Buffer
      |
      | memcpy
      v
Bounce Buffer

============================================================

Now bounce buffer contains:

HELLO

============================================================

Step 3

Give device DMA address.

============================================================

Device
   |
   v
0x01000000

============================================================

Step 4

Device reads data.

============================================================

Device
      |
      v
Bounce Buffer

============================================================

Transfer complete.

============================================================
CHAPTER 8
DMA_FROM_DEVICE
============================================================

This is even more important.

============================================================

Example:

Network receive

Disk read

USB receive

============================================================

Driver allocated:

Real Buffer

0x180000000

============================================================

Device cannot reach it.

============================================================

Linux allocates:

Bounce Buffer

0x01000000

============================================================

Device writes data.

============================================================

Device
   |
   v
Bounce Buffer

============================================================

Now bounce buffer contains:

Incoming Packet

============================================================

But driver later accesses:

0x180000000

============================================================

Driver knows nothing about bounce buffer.

============================================================

Therefore Linux must copy.

============================================================

Bounce Buffer
      |
      | memcpy
      v
Real Buffer

============================================================

Now driver sees packet where it expected it.

============================================================
CHAPTER 9
WHY CAN'T WE JUST KEEP USING THE BOUNCE BUFFER?
============================================================

Excellent question.

Suppose:

Driver allocated:

buf

============================================================

Driver later executes:

buf[0] = 42;

============================================================

Driver expects:

buf

to be authoritative memory.

============================================================

If Linux secretly replaces memory with bounce buffer:

Now there are two copies.

============================================================

Original Buffer

and

Bounce Buffer

============================================================

Which one is correct?

============================================================

Keeping both synchronized forever would require:

Every CPU write
Every CPU read
Every Device write
Every Device read

to be intercepted.

============================================================

That would be incredibly expensive.

============================================================

Instead Linux guarantees:

Driver always owns original buffer.

Bounce buffer is temporary.

============================================================
CHAPTER 10
WHY IS THE BOUNCE BUFFER REUSED?
============================================================

Another common confusion.

============================================================

Imagine:

Bounce Buffer #100

============================================================

Used by NIC.

============================================================

DMA completes.

============================================================

Question:

Why not keep it forever?

============================================================

Because bounce buffers are scarce.

============================================================

Suppose:

SWIOTLB Pool = 64MB

============================================================

Thousands of devices perform DMA.

============================================================

If every mapping kept its bounce buffer:

Pool exhausted quickly.

============================================================

Instead:

DMA starts
      |
      v
Allocate slot
      |
      v
Use slot
      |
      v
DMA finishes
      |
      v
Free slot
      |
      v
Reuse later

============================================================
PARKING LOT ANALOGY
============================================================

Think of bounce slots as parking spaces.

============================================================

+----+----+----+----+
| S1 | S2 | S3 | S4 |
+----+----+----+----+

============================================================

NIC uses S1.

============================================================

NIC leaves.

============================================================

S1 becomes free.

============================================================

NVMe can use S1 later.

============================================================

Shared pool.

Exclusive use while active.

============================================================
CHAPTER 11
WHY CAN'T TWO DEVICES SHARE THE SAME SLOT?
============================================================

Imagine:

NIC DMA WRITE

and

NVMe DMA READ

============================================================

Both using:

Bounce Slot #5

============================================================

Immediate corruption.

============================================================

Therefore:

One active mapping
      |
      v
One bounce slot owner

============================================================

Reuse happens over time.

Not simultaneously.

============================================================
CHAPTER 12
WHAT DOES A REAL IOMMU DO DIFFERENTLY?
============================================================

Real IOMMU:

============================================================

Device DMA Address
       |
       v
IOMMU Page Table
       |
       v
Real Buffer

============================================================

Example:

DMA Address

0x1000

============================================================

Translated to:

0x180000000

============================================================

Device accesses real memory directly.

============================================================

No bounce buffer.

No memcpy.

No duplication.

============================================================
SWIOTLB VS IOMMU
============================================================

SWIOTLB

Device
   |
   v
Bounce Buffer
   |
   v
Real Buffer

Requires copying.

============================================================

IOMMU

Device
   |
   v
IOMMU
   |
   v
Real Buffer

No copying.

============================================================
PERFORMANCE COMPARISON
============================================================

Direct DMA

Device -> Real Buffer

Fastest

============================================================

IOMMU

Device -> IOMMU -> Real Buffer

Very fast

============================================================

SWIOTLB

Device -> Bounce Buffer
                   |
                   v
              memcpy
                   |
                   v
             Real Buffer

Slowest

============================================================
WHEN SWIOTLB IS USED
============================================================

Typically:

No hardware IOMMU

AND

RAM above device DMA limit

============================================================

Common examples:

Old PCI devices

32-bit DMA controllers

Virtual machines

Early boot

Crash kernels

Restricted DMA environments

============================================================
COMPLETE DMA_TO_DEVICE FLOW
============================================================

Driver Buffer
      |
      v
dma_map_single()
      |
      v
Allocate Bounce Slot
      |
      v
Copy Driver Buffer
      |
      v
Bounce Buffer
      |
      v
Device DMA READ
      |
      v
dma_unmap_single()
      |
      v
Free Bounce Slot

============================================================
COMPLETE DMA_FROM_DEVICE FLOW
============================================================

Driver Buffer
      |
      v
dma_map_single()
      |
      v
Allocate Bounce Slot
      |
      v
Device DMA WRITE
      |
      v
Bounce Buffer
      |
      v
dma_unmap_single()
      |
      v
Copy Back To Driver Buffer
      |
      v
Free Bounce Slot

============================================================
MENTAL MODEL
============================================================

Think of SWIOTLB as:

A temporary loading dock.

============================================================

Warehouse
(Real Buffer)

      ^
      |
      |
Loading Dock
(Bounce Buffer)

      ^
      |
      |
Truck
(Device)

============================================================

Truck cannot reach warehouse.

So goods are temporarily placed
on the loading dock.

After transfer:

Loading dock becomes available
for another truck.

============================================================
ONE-LINE SUMMARY
============================================================

SWIOTLB solves DMA addressability problems by allocating
temporary low-memory bounce buffers that devices can access,
copying data between the real high-memory buffer and the
bounce buffer as needed, then returning the bounce buffer
to a shared pool for reuse after the DMA operation finishes.
============================================================
```

