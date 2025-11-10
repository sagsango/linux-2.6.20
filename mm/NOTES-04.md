# VMALLOC FLOW DIAGRAM
                 Userspace / Kernel Subsystem
                         calls
                              |
                              v
                        +-------------+
                        |  vmalloc()  |
                        +-------------+
                                |
                                v
        ----------------------------------------------------------------
        STEP 1: Reserve virtual address range from the VMALLOC region
        ----------------------------------------------------------------
                                |
                                v
                   +-------------------------+
                   |  get_vm_area(size,flags)|  
                   +-------------------------+
                                |
                                v
        +--------------------------------------------------------------+
        | 1. Find free area in the global vmalloc address space        |
        |    Uses: vmlist (global linked list of vmalloc areas)        |
        |                                                              |
        | 2. Insert into vmlist                                        |
        |    → struct vm_struct { start, end, flags, pages[], ... }    |
        |                                                              |
        | 3. Allocate array of struct page* pointers (vm->pages[])     |
        +--------------------------------------------------------------+
                                |
                                v
             vmalloc returns kernel VA reserved but NOT backed by pages


        ----------------------------------------------------------------
        STEP 2: Allocate physical pages (lazily or directly)
        ----------------------------------------------------------------
                                |
                                v
                +---------------------------------------+
                | alloc_page()/alloc_pages(GFP_KERNEL)  |
                +---------------------------------------+
                                |
                                v
       +---------------------------------------------------------------+
       | These come from:                                              |
       |   • Buddy allocator                                           |
       |   • Correct zone selected via GFP flags                       |
       |                                                               |
       | Pages are NOT contiguous physically                           |
       | (main reason vmalloc exists!)                                 |
       +---------------------------------------------------------------+
                                |
                                v
           vm->pages[i] = struct page* for each allocated page


        ----------------------------------------------------------------
        STEP 3: Build page tables for the virtual area
        ----------------------------------------------------------------
                                |
                                v
                   +------------------------------+
                   |  map_vm_area(vm, prot)       |
                   +------------------------------+
                                |
                                v
               +-------------------------------------------+
               | Walk kernel page tables:                  |
               |   pgd → pmd → pte                         |
               | Allocate intermediate levels if needed    |
               +-------------------------------------------+
                                |
                                v
          For each page:
              set_pte(pte, mk_pte(page, prot))

                                |
                                v
     After this, kernel virtual address → physical page mapping is valid


        ----------------------------------------------------------------
        STEP 4: Ensure mappings visible on all CPUs (important)
        ----------------------------------------------------------------
                                |
                                v
                +-------------------------------+
                |   vmalloc_sync_all()          |
                +-------------------------------+
                                |
                                v
     Required on older kernels w/o global TLB or lazy TLB mechanisms
     Syncs page table updates to all CPU pgd copies.


        ----------------------------------------------------------------
        FINAL: return kernel virtual address
        ----------------------------------------------------------------


# FULL REVERSE FLOW — VUNMAP
                    +-----------+
                    |  vfree()  |
                    +-----------+
                           |
                           v
                +---------------------+
                | remove vm_struct    |
                | (unlink from vmlist)|
                +---------------------+
                           |
                           v
         Unmap page tables: unmap_vm_area()
                           |
                           v
                Free struct page* array
                           |
                           v
                Free physical pages (buddy)
                           |
                           v
                  ARP: TLB flush
                           |
                           v
                    Done


# DETAILED CALL GRAPH (2.6.20 accurate)
vmalloc()
 └── __vmalloc()
      ├── get_vm_area()
      │     ├── find_vm_area()
      │     ├── alloc vm_struct
      │     └── insert into vmlist
      ├── alloc_vm_area_pages()   (sometimes in callers)
      │     └── alloc_page()
      ├── map_vm_area()
      │     ├── alloc page tables (pgd/pmd/pte)
      │     └── set_pte()
      └── vmalloc_sync_all()


# Kernel Virtual Address Space
----------------------------------------------------------
 |   code/data   |  modules  |   vmalloc area   |   fixmap |
----------------------------------------------------------
                      ^               ^
                      |               |
             vmalloc area       ioremap(), fixmaps


# how vmalloc() creates vm_struct (vm_sturct : see vmalloc.h)
           vmalloc(size)
                │
                ▼
       Round size to page aligned
                │
                ▼
       __vmalloc_area_node()
                │
                ▼
        __get_vm_area(size, flags)
                │
                ▼
+--------------------------------------+
| 1) Search vmlist (linked list)       |
|    for a free gap in vmalloc_space   |
+--------------------------------------+
                │ found gap
                ▼
  Allocate struct vm_struct (kmalloc)
                │
                ▼
   Fill fields: addr, size, flags…
                │
                ▼
 Insert into vmlist (ordered by addr)
                │
                ▼
    Allocate pages: alloc_pages()
         (nr_pages times)
                │
                ▼
   pages[] array is filled with struct page*
                │
                ▼
 map_vm_area(vm_struct, prot)
   → walk page table
   → allocate page table pages
   → install PTEs mapping pages[i]
                │
                ▼
          return vm->addr



# how vmalloc areas sit inside kernel vmalloc region
  high memory
  --------------------------- 0xFFFFFFFF
      fixmap
  ---------------------------
      vmalloc area
  -----------------------------------------------------
  | vmalloc region                                     |
  |                                                    |
  | [vm1][free space][vm2][free space][vm3] ...        |
  -----------------------------------------------------
  --------------------------- VMALLOC_START (~0xF0000000)

      lowmem direct mapping (phys=virt- PAGE_OFFSET)
  -----------------------------------------------------
  --------------------------- PAGE_OFFSET (0xC0000000)



# Step-by-step walk-through of get_vm_area()
get_vm_area(size, flags)
    │
    ▼
Compute total size:
   size_aligned = PAGE_ALIGN(size)
   area_size = size_aligned + VM_STRUCT padding
    │
    ▼
Lock vmlist (spinlock)
    │
    ▼
Scan vmlist:
    prev = NULL
    for each vm_struct in vmlist:
         gap_start = (prev == NULL) ? VMALLOC_START : prev->addr + prev->size
         gap_end   = vm->addr
         if (gap_end - gap_start >= area_size):
               found free hole → break
         prev = vm
    │
    ▼
If no vm_struct is at end:
    check last gap: end_of_vmalloc - last_vm->end
    │
    ▼
If still no gap → return NULL
    │
    ▼
Allocate a new struct vm_struct
    │
    ▼
Set:
   vm->addr = gap_start
   vm->size = area_size
   vm->flags = flags
    │
    ▼
Insert vm into vmlist at correct sorted position
    │
    ▼
Unlock vmlist
    │
    ▼
return vm


# how vmalloc maps pages into page tables
vmalloc() allocated vm->pages[]   (struct page*)
              │
              ▼
map_vm_area(vm_struct, prot)
              │
              ▼
pgd = pgd_offset_k(vm->addr)
for each virtual page:
        if pud/pmd/pte table missing:
              create new one (alloc_pages)
        ptep = get pte entry
        set_pte(ptep, mk_pte(vm->pages[i], prot))
        flush_cache_vmap()
              │
              ▼
flush_tlb_kernel_range(vm->addr, vm->addr+vm->size)


# Comparison: vmalloc vs kmalloc vs ioremap
kmalloc:
Returns physically contiguous memory.
Fast.
Allocated from slab caches.
Direct-mapped (linear mapping).
Virtual = PAGE_OFFSET + phys_addr.
Cannot allocate large sizes (fragmentation).


vmalloc:
Returns virtually contiguous memory.
Physically non-contiguous allowed.
Slow (creates page table entries).
Uses vmlist.
Used for:
 - large buffers
 - modules
 - stacks
 - mappings created via vmap()

# ioremap
Maps device physical memory (MMIO) into kernel virtual space.
Uses same vmalloc area.
vm_struct.phys_addr stores physical base.
No page allocation; just maps device pages.
Special flags (uncached/wc).




