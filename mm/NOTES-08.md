# Where mempool fits in
mempool is not called by the allocator directly; rather, it’s used by subsystems that require guaranteed allocations. Examples:
Block layer: For request buffers
SCSI drivers: Pre-allocated command structures
Networking code: Pre-allocated sk_buff buffers

Critical Kernel Code
        │
        ▼
Calls mempool_alloc(pool)
        │
        ▼
If pool has free object → returns immediately
Else → alloc_fn() is called (usually kmalloc)
        │
        ▼
kmalloc(size, GFP_KERNEL)
        │
        ▼
alloc_pages(order=n)
        │
        ▼
Buddy allocator → returns physical pages
        │
        ▼
kmalloc builds object from pages → returns pointer
        │
        ▼
mempool_alloc → returns pointer to critical code



# Detailed Flow Diagram
[Critical Kernel Function]
       │
       ▼
mempool_alloc(pool)
       │
       ▼
+--------------------------+
| Is pool empty?           |
+--------------------------+
| Yes → call pool->alloc_fn|----+
| No  → return obj from pool|   |
+--------------------------+   |
                                |
                                ▼
                       [alloc_fn = kmalloc(size, GFP_KERNEL)]
                                │
                                ▼
                       kmalloc checks size:
                                │
      +-------------------------+--------------------------+
      │                                                 │
size <= KMALLOC_MAX_SIZE                           size > KMALLOC_MAX_SIZE
      │                                                 │
      ▼                                                 ▼
slab allocator                                      vmalloc (if required)
      │
      ▼
slab cache → alloc pages from buddy (order depends on size)
      │
      ▼
buddy allocator → find free pages (order 0 or higher)
      │
      ▼
Physical memory pages (struct page *)
      │
      ▼
slab builds object → returns kernel pointer
      │
      ▼
kmalloc returns pointer → mempool_alloc returns to caller



# Memory Mapping Perspective
Physical Memory (non-contiguous)
──────────────────────────────
[ PFN 100 ][ PFN 205 ][ PFN 30 ][ PFN 87 ] ...

Buddy Allocator:
- Manages free pages per zone
- Allocates pages based on order
- Returns struct page *

Slab Allocator:
- Carves kmalloc objects from pages
- Uses caches for different sizes
- Returns virtual pointer to object

mempool:
- Stores pre-allocated objects
- Returns guaranteed object pointer to caller
