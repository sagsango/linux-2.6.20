# What is SLOB?
SLOB in Linux 2.6.20:
Small, simple slab allocator for embedded/low-memory systems.
Works as a linked list of blocks (slob_block) within pages from the buddy allocator.
kmalloc/kfree sits on top of SLOB for general kernel memory allocation.
Big allocations (≥ PAGE_SIZE) go directly to __get_free_pages (buddy system).

Kernel Code
   │
   ▼
kmalloc(size, GFP_KERNEL)
   │
   ├─ size < PAGE_SIZE → slob_alloc() → allocate block from slob pages
   │
   └─ size ≥ PAGE_SIZE → __get_free_pages() → add bigblock → return pointer



# Data Structures in Flow
1. slob_block
    Represents a free or allocated block.
    Contains units and next.
2. bigblock
    Represents large allocations (≥ PAGE_SIZE).
    Contains pages, order, next.
3. slobfree
    Circular linked list of all free blocks.

# Allocation Flow
kmalloc(size, gfp_mask)
   │
   ├─ if size < PAGE_SIZE → call slob_alloc(size + SLOB_UNIT, gfp, align=0)
   │
   └─ else → allocate bigblock:
           ├─ allocate bigblock struct via slob_alloc()
           ├─ order = find_order(size)
           └─ pages = __get_free_pages(gfp, order)


slob_alloc(size, gfp, align)
   │
   ▼
units = SLOB_UNITS(size)
   │
   ▼
Iterate slobfree circular linked list:
   ├─ If cur->units >= units + delta (delta = alignment adjustment):
   │     ├─ Fragment block if needed
   │     └─ Allocate block → return pointer
   │
   └─ If no free block found:
         ├─ __get_free_page(gfp)
         ├─ slob_free(new_page, PAGE_SIZE)
         └─ Retry allocation

slob_free(ptr, size)
   │
   ▼
Convert ptr to slob_block
   │
   ▼
Find reinsertion point in circular list
   │
   ▼
Coalesce with neighboring free blocks if contiguous
   │
   └─ If whole page free → free_page() → buddy allocator


# Physical Memory Path
Physical RAM
   │
   ▼
Buddy Allocator (zone: DMA/NORMAL/HIGHMEM)
   │
   ├─ alloc_pages(order=0) → small allocations for slob pages
   │
   └─ alloc_pages(order=n) → big allocations (≥ PAGE_SIZE)
   │
   ▼
Pages mapped to:
   ├─ slob_page → subdivided into slob_blocks
   └─ bigblock → returned directly
   │
   ▼
kmalloc pointer returned to kernel code


# SLOB Flow Diagram
[Kernel Code / Driver]
        │
        ▼
kmalloc(size, GFP_KERNEL)
        │
        ├─ size < PAGE_SIZE
        │      │
        │      ▼
        │  slob_alloc(size + header)
        │      │
        │      ▼
        │  Iterate slobfree list → find free block
        │      │
        │      ├─ Found → allocate → return pointer
        │      │
        │      └─ Not found → __get_free_page() → slob_free(new_page) → retry
        │
        └─ size ≥ PAGE_SIZE
               │
               ▼
          Allocate bigblock structure via slob_alloc()
               │
               ▼
          order = find_order(size)
               │
               ▼
          pages = __get_free_pages(gfp, order)
               │
               ▼
          Add bigblock to bigblocks list → return pages pointer

Physical Memory:
  ┌─────────────────────────┐
  │        RAM              │
  │                         │
  │  Buddy Allocator Zones  │
  │ ┌─────────────────────┐ │
  │ │ slob pages (order=0)│ │
  │ │ subdivided into     │ │
  │ │ slob_blocks         │ │
  │ └─────────────────────┘ │
  │ ┌─────────────────────┐ │
  │ │ bigblocks (order=n) │ │
  │ │ directly returned   │ │
  │ └─────────────────────┘ │
  └─────────────────────────┘



# Memory Management Hierarchy
 ┌───────────────────────────────┐
 │      Kernel / Driver Code      │
 │   (needs memory allocation)   │
 └───────────────────────────────┘
                 │
                 ▼
          +----------------+
          | Memory APIs    |
          | kmalloc(),     |
          | vmalloc(),     |
          | kzalloc(),     |
          | kfree()        |
          +----------------+
                 │
 ┌───────────────┴────────────────┐
 │                               │
 ▼                               ▼
Small allocations (< PAGE_SIZE)  Large allocations (≥ PAGE_SIZE)
 │                               │
 │                               ▼
 │                        __get_free_pages(gfp, order)
 │                               │
 │                               ▼
 │                        Bigblock linked list
 │                               │
 │                               ▼
 │                        Buddy allocator (order ≥ 0)
 │                               │
 │                               ▼
 │                        Physical RAM pages
 │
 ▼
 ┌────────────────────────────────────────────┐
 │                SLOB / Slab Allocator       │
 │ Small allocations are handled here:        │
 │                                            │
 │ ┌──────────────────────────────────────┐   │
 │ │ slob pages from buddy allocator      │   │
 │ │ (order=0)                            │   │
 │ │ ┌──────────────────────────────────┐ │   │
 │ │ │ slob_block linked list           │ │   │
 │ │ │  - units, next                   │ │   │
 │ │ │  - coalesced on free             │ │   │
 │ │ └──────────────────────────────────┘ │   │
 │ └──────────────────────────────────────┘   │
 │                                            │
 │ Slab allocator is built on top of SLOB or  │
 │ directly on buddy pages for structured     │
 │ caches(kmem_cache_create, kmem_cache_alloc)│
 └────────────────────────────────────────────┘
                 │
                 ▼
         kmalloc() returns pointer
                 │
                 ▼
        Kernel uses memory block

# Flow (with Zones & Buddy)
Kernel code calls kmalloc(size, gfp_mask)
       │
       ├── size < PAGE_SIZE → small allocation → SLOB allocator
       │       │
       │       ├─ search slobfree list for free block
       │       ├─ if found → allocate block
       │       ├─ if not → __get_free_page(gfp) from buddy
       │       └─ slob_free coalesces and returns pointer
       │
       └── size ≥ PAGE_SIZE → big allocation → __get_free_pages(gfp, order)
               │
               ├─ allocated pages added to bigblocks list
               └─ pointer returned directly to kernel

SLOB / SLAB Allocator
       │
       └─ Can provide kmem_cache objects (structured allocations)
              │
              ├─ ctor / dtor called
              └─ uses SLOB for underlying memory (< PAGE_SIZE)

Buddy Allocator (page-level allocation)
       │
       ├─ Zones: DMA / NORMAL / HIGHMEM
       │
       ├─ alloc_pages(order) called by:
       │       ├─ SLOB (__get_free_page)
       │       ├─ vmalloc (for each page)
       │       ├─ big allocations (> PAGE_SIZE)
       │
       └─ Returns physical pages

Physical RAM
       │
       ├─ Pages are mapped into kernel address space
       │      ├─ kmalloc / SLOB: small blocks within pages
       │      ├─ vmalloc: non-contiguous pages mapped into virtual memory
       │      └─ bigblocks: contiguous physical pages


# Unified Memory Management Flow Diagram
          [Kernel Code / Driver]
                     │
                     ▼
               Memory APIs
        ┌─────────┬─────────┐
        │         │         │
   kmalloc()   vmalloc()   kzalloc()
        │         │         │
 ┌──────┴──────┐  │         │
 │ Small alloc │  │         │
 │ (< PAGE_SIZE)│  │         │
 └──────┬──────┘  │         │
        │         │         │
        ▼         ▼         ▼
     SLOB Allocator   Non-contiguous pages
        │             │ mapped via vmalloc
        │             │
        └─────────────┘
               │
               ▼
        Buddy Allocator
   (alloc_pages(order), zones DMA/NORMAL/HIGHMEM)
               │
               ▼
          Physical RAM
   ┌───────────┬───────────┐
   │ slob pages│ bigblocks │
   │ (order 0) │ (order n) │
   └───────────┴───────────┘
               │
               ▼
     Kernel receives pointer
        (usable memory)


