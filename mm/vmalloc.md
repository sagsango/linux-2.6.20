/*
 * Linux 2.6.20 — mm/vmalloc.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (vmalloc vs kmalloc)
 *  - Virtual vs physical contiguity
 *  - vm_struct + vmlist
 *  - get_vm_area() allocator (virtual space)
 *  - __vmalloc() flow (CRITICAL)
 *  - page table mapping (map_vm_area)
 *  - unmapping (vunmap, vfree)
 *  - user mapping (remap_vmalloc_range)
 *
 */

/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
vmalloc provides:

    VIRTUALLY CONTIGUOUS MEMORY

using:

    NON-CONTIGUOUS PHYSICAL PAGES

This is opposite of kmalloc:

    kmalloc → physically contiguous
    vmalloc → virtually contiguous
*/


/***************************************************************
 * 1. WHY vmalloc EXISTS
 ***************************************************************/

/*
Problem:

Large contiguous physical memory is hard to allocate due to fragmentation.

Solution:

Allocate small pages anywhere + map them contiguously in VA space.
*/


/***************************************************************
 * 2. CORE DATA STRUCTURE: vm_struct
 ***************************************************************/

/*
Represents one vmalloc allocation:

    addr      → base virtual address
    size      → size of mapping
    pages     → array of struct page*
    nr_pages  → number of pages
    flags     → VM_ALLOC, VM_IOREMAP, etc
*/


/***************************************************************
 * 3. GLOBAL LIST: vmlist
 ***************************************************************/

/*
All vmalloc areas tracked in linked list:

    vmlist

Protected by:

    vmlist_lock (rwlock)
*/


/***************************************************************
 * 4. STEP 1: RESERVE VIRTUAL SPACE
 ***************************************************************/

/*
get_vm_area(size, flags)

→ finds free region in VMALLOC space
→ inserts vm_struct into vmlist
*/


/*
Algorithm:

walk vmlist
find gap
insert new area
*/


/***************************************************************
 * 5. STEP 2: ALLOCATE PAGES
 ***************************************************************/

/*
__vmalloc_area_node():

for each page:
    alloc_page(gfp)
*/


/*
These pages are NOT contiguous physically
*/


/***************************************************************
 * 6. STEP 3: MAP PAGES (CRITICAL)
 ***************************************************************/

/*
map_vm_area(area, prot, &pages)

Creates page table entries:

    VA → page[i]
*/


/*
Flow:

PGD → PUD → PMD → PTE

set_pte(page)
*/


/***************************************************************
 * 7. FINAL FLOW (__vmalloc)
 ***************************************************************/

/*
vmalloc(size):

1. reserve VA (get_vm_area)
2. allocate pages
3. map pages
4. return virtual address
*/


/***************************************************************
 * 8. UNMAPPING (vfree / vunmap)
 ***************************************************************/

/*
vfree():

1. remove_vm_area()
2. unmap_vm_area()
3. free pages
4. free metadata
*/


/*
unmap_vm_area():

walk page tables
clear PTEs
flush TLB
*/


/***************************************************************
 * 9. vmap()
 ***************************************************************/

/*
Maps EXISTING pages into contiguous VA

Used when pages already allocated elsewhere
*/


/***************************************************************
 * 10. GUARD PAGE
 ***************************************************************/

/*
Each allocation adds extra PAGE_SIZE

→ prevents overflow bugs
*/


/***************************************************************
 * 11. USER MAPPING (CRITICAL)
 ***************************************************************/

/*
remap_vmalloc_range():

Maps vmalloc memory into userspace VMA

Flow:

for each page:
    vmalloc_to_page()
    vm_insert_page()
*/


/***************************************************************
 * 12. CACHE + TLB
 ***************************************************************/

/*
After mapping:

    flush_cache_vmap()

After unmapping:

    flush_tlb_kernel_range()
*/


/***************************************************************
 * 13. IMPORTANT FLAGS
 ***************************************************************/

/*
VM_ALLOC      → normal vmalloc
VM_IOREMAP    → device mapping
VM_USERMAP    → user visible
VM_VPAGES     → pages array vmalloc'ed
*/


/***************************************************************
 * 14. KEY INSIGHT
 ***************************************************************/

/*
vmalloc = "build virtual memory manually"

You:
    allocate pages
    create page tables
*/


/***************************************************************
 * 15. FULL FLOW
 ***************************************************************/

/*
vmalloc():
    ↓
get_vm_area()
    ↓
alloc_page() x N
    ↓
map_vm_area()
    ↓
return VA

vfree():
    ↓
unmap_vm_area()
    ↓
free pages
*/


/***************************************************************
 * 16. INTERVIEW MODEL
 ***************************************************************/

/*
vmalloc allocates virtually contiguous memory by allocating
non-contiguous pages and mapping them via page tables.
*/


/***************************************************************
 * 17. ONE-LINE SUMMARY
 ***************************************************************/

/*
vmalloc builds contiguous virtual memory using non-contiguous physical pages
via explicit page table mappings.
*/


/***************************************************************
 * END
 ***************************************************************/
