# TOP–DOWN FULL SYSTEM FLOW DIAGRAM
┌──────────────────────────────────────────────┐
│            KERNEL CODE / DRIVERS             │
│   (calls kmalloc, kfree, vmalloc, caches)    │
└──────────────────────────────────────────────┘
                      │
                      ▼
        ┌──────────────────────────────────┐
        │        kmalloc(size, gfp)        │
        └──────────────────────────────────┘
                      │
     ┌────────────────┼────────────────────────────┐
     │                │                            │
     ▼                ▼                            ▼
Small alloc       Cache-based alloc          Large alloc
(< PAGE_SIZE)     kmem_cache_alloc()         (≥ PAGE_SIZE)
     │                │                            │
     ▼                ▼                            ▼
┌──────────────┐  ┌──────────────┐       ┌─────────────────┐
│  SLOB        │  │  SLAB-cache  │       │  bigblock alloc │
│  allocator   │  │ (kmem_cache) │       │ __get_free_pages│
└──────────────┘  └──────────────┘       └─────────────────┘
     │                │                            │
     ▼                ▼                            ▼
     └─────── both need physical pages ────────────┘
                      │
                      ▼
        ┌──────────────────────────────────┐
        │        buddy allocator           │
        │     alloc_pages(order=N)         │
        └──────────────────────────────────┘
                      │
          selects zone based on gfp_mask:
                      ▼
        ┌──────────────────────────────────┐
        │   ZONE_DMA / ZONE_NORMAL /       │
        │   ZONE_DMA32 / ZONE_HIGHMEM      │
        └──────────────────────────────────┘
                      │
                      ▼
        ┌──────────────────────────────────┐
        │       FREE AREA LISTS            │
        │ free_area[0..MAX_ORDER]          │
        │ (blocks: 1p,2p,4p,8p,...1024p)   │
        └──────────────────────────────────┘
                      │
                      ▼
        ┌──────────────────────────────────┐
        │       PHYSICAL MEMORY            │
        │  struct page array + RAM pages   │
        └──────────────────────────────────┘


# HORIZONTAL PIPELINE (Detailed)
[KERNEL CODE]
     │
     ▼
kmalloc(size)
     │
     ├── if size < PAGE_SIZE
     │         ▼
     │    SLOB allocator
     │         │
     │         ├─ find free slob_block inside slob pages
     │         └─ if none:
     │               └─ __get_free_page() → buddy allocator
     │
     ├── if kmalloc uses SLAB cache
     │         ▼
     │    SLAB allocator (kmem_cache)
     │         │
     │         ├─ allocate object from slab freelist
     │         └─ if slab empty → grow slab:
     │               └─ alloc_pages(order) → buddy allocator
     │
     └── if size ≥ PAGE_SIZE
               ▼
         bigblock allocation
               │
               └─ __get_free_pages(order) → buddy allocator


buddy allocator
     │
     ▼
select appropriate ZONE
     │
     ▼
search free_area[order]
     │
     ├─ if free → allocate block
     │
     └─ if not free:
            └─ take higher order block & split
     │
     ▼
physical pages


# DETAILED “STACKED” ARCHITECTURE
┌────────────────────────────────────────────┐
│             KERNEL MEMORY API              │
├────────────────────────────────────────────┤
│ kmalloc, kzalloc, kfree, kmem_cache_alloc  │
└────────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────┐
│       FRONT-END ALLOCATORS (3 paths)       │
├────────────────────────────────────────────┤
│ 1) SLOB allocator (small objects <4KB)     │
│ 2) SLAB allocator (caches, structs)        │
│ 3) bigblocks (__get_free_pages)            │
└────────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────┐
│               PAGE ALLOCATOR               │
│             The Buddy System               │
├────────────────────────────────────────────┤
│ alloc_pages(order)                         │
│ free_area[0..10]                           │
│ split/merge buddies                        │
└────────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────┐
│                MEMORY ZONES                │
├────────────────────────────────────────────┤
│ ZONE_DMA        (<16MB)                    │
│ ZONE_DMA32      (<4GB)                     │
│ ZONE_NORMAL     (direct-mapped RAM)        │
│ ZONE_HIGHMEM    (high memory not direct)   │
└────────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────┐
│              PHYSICAL MEMORY               │
├────────────────────────────────────────────┤
│ RAM pages (struct page array)              │
└────────────────────────────────────────────┘

# Under-the-Hood: SLOB + SLAB + Buddy
slob_alloc()
     │
     ├─ search slobfree list
     │
     └─ if not found:
            └─ __get_free_page()
                 │
                 ▼
               Buddy


kmem_cache_alloc()
     │
     ├─ free object in slab?
     │       └─ return
     │
     └─ else grow slab:
            └─ alloc_pages(order)
                 │
                 ▼
               Buddy


size ≥ PAGE_SIZE
     │
     ▼
__get_free_pages(order)
     │
     ▼
Buddy


# GRAND-DIAGRAM (Everything connected)
[KERNEL CODE]
   │
   ▼
kmalloc(size)
   │
   ├─ size < PAGE_SIZE → SLOB allocator
   │         │
   │         ├─ find slob_block
   │         └─ if none → __get_free_page()
   │
   ├─ kmem_cache_alloc → SLAB allocator
   │         │
   │         ├─ object from slab freelist
   │         └─ if slab empty → alloc_pages(order)
   │
   └─ size ≥ PAGE_SIZE → bigblock
             │
             └─ __get_free_pages(order)
   │
   ▼
Buddy allocator (alloc_pages)
   │
   ▼
Select ZONE (DMA / DMA32 / NORMAL / HIGHMEM)
   │
   ▼
free_area[] lists per order
   │
   ├─ direct block
   └─ or split higher-order block
   │
   ▼
Physical RAM pages
   │
   ▼
kmalloc returns pointer to caller
