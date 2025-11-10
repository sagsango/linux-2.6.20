# The buddy system manages all page frames (4 KB each).
Physical Memory
──────────────────────────────────────────────
| PFN 0 | PFN 1 | PFN 2 | PFN 3 | ... | PFN N |
──────────────────────────────────────────────
   ↓       ↓       ↓       ↓          ↓
 4 KB    4 KB    4 KB    4 KB        4 KB
 pages   pages   pages   pages       pages

Each page is represented by: struct page

# the Buddy System Hierarchy (Orders)
Linux maintains free lists in powers of two sizes: order = k  →  block size = 2^k pages

order 0 → 1 page (4 KB)
order 1 → 2 pages (8 KB)
order 2 → 4 pages (16 KB)
order 3 → 8 pages (32 KB)
order 4 → 16 pages (64 KB)
...
order 10 → 1024 pages (4 MB)

# free_area[order] Structure Diagram
present inside the zone struct
struct free_area free_area[MAX_ORDER];
ZONE_NORMAL
──────────────────────────────────────────────
free_area[0] → list of 4KB blocks
free_area[1] → list of 8KB blocks
free_area[2] → list of 16KB blocks
free_area[3] → list of 32KB blocks
...
free_area[10] → list of 4MB blocks
──────────────────────────────────────────────


# Allocating a block (alloc_pages(order))
alloc_pages(order=2)
      │
      ▼
Check free_area[2] → do we have 16KB block?
      │ yes
      ▼
remove block from list
return block (4 pages)


# If a higher order is found:
128KB (order 5)
→ split into 64KB (order 4)
→ split into 32KB (order 3)
→ split into 16KB (order 2)

[ 128 KB block ]  (order 5)
       ↓ split
[64] [64]         (order 4)
       ↓ split one of them
[32][32] [64]     (order 3)
       ↓ split one 32
[16][16][32][64]  (order 2)


# Freeing Pages (free_pages)
free_pages(order=2)
     │
     ▼
Find buddy page number using:
buddy = page_number XOR (1 << order)
     │
     ▼
If buddy is free and same order:
     combine → new block order 3
     try merging again
Else:
     put this block into free_area[2]

# Buddy Coalescing Diagram
Pages:
0 1 2 3 | 4 5 6 7 | 8 9 10 11 | 12 13 14 15

Before:
order 2:  [0–3]   [4–7]   [8–11]   [12–15]

Freeing [4–7]

Merge:
order 3:  [0–7]   [8–11]  [12–15]


# Master Diagram: The Entire Buddy System
                 +------------------------------+
                 |    alloc_pages(order=N)      |
                 +------------------------------+
                              │
             ┌───────────────┴────────────────┐
             ▼                                ▼
   Is free_area[N] non-empty?        Try higher order
             │                                │
          yes/no                            split big block
             │                                │
             ▼                                ▼
    return block                     distribute remainders
                                             │
                                             ▼
                                    return desired block



           free_pages(block, order)
                     │
                     ▼
          find buddy using XOR
                     │
       Is buddy free and same order?
                 │        │
                yes       no
                 │        │
 merge blocks ←──┘   put into free_area[order]
                 │
            increase order
                 │
         try merging again



# Buddy → Slab → kmalloc Stack Works
User code
   │
kmalloc(size)
   │
slab allocator (kmem_cache)
   │
allocates slabs using
alloc_pages(order=X)
   │
buddy allocator
   │
physical pages



# Buddy → vmalloc Works
vmalloc(size)
    │
    ▼
round up size → n_pages
    │
    ▼
get_vm_area(size) → find virtual hole
    │
    ▼
create vm_struct:
    ┌─────────────────────┐
    │ vm_struct.pages[]   │ ← will store struct page* for each page
    │ vm_struct.addr      │ ← virtual start address
    └─────────────────────┘
    │
    ▼
for each page i:
    alloc_pages(order=0)  ← buddy allocator returns 1 page
        │
        ▼
    store struct page* in vm_struct.pages[i]
    │
    ▼
map page into page table at virtual addr:
    remap_page_range(vm_struct.addr + i*PAGE_SIZE, page[i])
    │
    ▼
return vm_struct.addr to caller
