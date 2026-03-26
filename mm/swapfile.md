/*
 * Linux 2.6.20 — mm/swapfile.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (swap device management)
 *  - swap_info_struct (CRITICAL)
 *  - swap_map (reference counts of swap slots)
 *  - allocation: get_swap_page() + scan_swap_map()
 *  - free: swap_free(), swap_entry_free()
 *  - swapoff flow (VERY IMPORTANT)
 *  - swapon flow (VERY IMPORTANT)
 *  - extent mapping (file → disk blocks)
 *  - relation to swap_state.c and page_io.c
 *
 * Source: user-provided swapfile.c fileciteturn12file0
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
This file manages:

    - Swap devices (files/partitions)
    - Allocation of swap slots
    - Reference counting of swap entries
    - swapon / swapoff system calls

This is the "BACKEND" of swap.

Compare:

    swap.c          → LRU + page lifecycle
    swap_state.c    → swap cache (entry → page)
    swapfile.c      → swap device + slot management
    page_io.c       → actual I/O
*/


/*
Mental model:

    swapfile.c = "disk space manager for swapped pages"
*/


/***************************************************************
 * 1. CORE DATA STRUCTURE: swap_info_struct
 ***************************************************************/

/*
Each swap device has:

    struct swap_info_struct

Stored in:

    swap_info[MAX_SWAPFILES]
*/


/*
Important fields:

    swap_map[]
        → array indexed by offset
        → stores reference count of each swap slot

    lowest_bit / highest_bit
        → search range optimization

    inuse_pages
        → number of used slots

    pages
        → total usable pages

    bdev
        → block device

    extent_list
        → file → disk mapping (for swapfile)

    flags
        → SWP_USED, SWP_WRITEOK, SWP_SCANNING
*/


/***************************************************************
 * 2. swap_map (CRITICAL)
 ***************************************************************/

/*
swap_map[offset] = reference count of that swap slot

Values:

    0 → free slot
    >0 → used (count = number of PTEs + swap cache)
    SWAP_MAP_BAD → unusable
*/


/*
Meaning:

swap_map is like a reverse page table for swap.
*/


/***************************************************************
 * 3. get_swap_page() (VERY IMPORTANT)
 ***************************************************************/

/*
Function:

    get_swap_page()

Purpose:

    allocate a free swap slot
*/


/*
Flow:

1. check nr_swap_pages
2. iterate swap devices (priority order)
3. for each device:
       call scan_swap_map()
4. if found:
       return swp_entry(type, offset)
*/


/*
Meaning:

This is the allocator for swap slots
*/


/***************************************************************
 * 4. scan_swap_map() (CRITICAL ALGORITHM)
 ***************************************************************/

/*
Goal:

    find free slot in swap_map[]
*/


/*
Optimization:

    cluster allocation (SWAPFILE_CLUSTER = 256)

Why?

    reduce disk seek
*/


/*
Flow:

if cluster available:
    allocate sequential slots
else:
    scan for free slot

update:
    lowest_bit / highest_bit
    inuse_pages
*/


/*
Key idea:

    locality → better I/O performance
*/


/***************************************************************
 * 5. swap_free()
 ***************************************************************/

/*
Free a swap entry
*/


/*
Flow:

1. lookup swap_info via entry
2. decrement swap_map[offset]
3. if becomes 0:
       mark slot free
       update stats
*/


/***************************************************************
 * 6. swap_duplicate()
 ***************************************************************/

/*
Increment reference count of swap entry
*/


/*
Used when:

    page enters swap cache
    multiple PTEs reference same swap entry
*/


/***************************************************************
 * 7. remove_exclusive_swap_page()
 ***************************************************************/

/*
If page is only user of swap entry → remove swap backing
*/


/*
Flow:

if swap_map[offset] == 1:
    delete_from_swap_cache()
    swap_free()
*/


/*
Meaning:

Avoid unnecessary swap usage
*/


/***************************************************************
 * 8. SWAPON FLOW (VERY IMPORTANT)
 ***************************************************************/

/*
Function:

    sys_swapon()
*/


/*
Flow:

1. open file/device
2. read swap header
3. validate signature (SWAPSPACE2)
4. allocate swap_map
5. setup extents (file → disk mapping)
6. initialize swap_info_struct
7. insert into swap_list (priority order)
8. mark SWP_ACTIVE
*/


/*
Meaning:

    make device usable for swapping
*/


/***************************************************************
 * 9. SWAPOFF FLOW (VERY IMPORTANT)
 ***************************************************************/

/*
Function:

    sys_swapoff()
*/


/*
Flow:

1. remove swap device from swap_list
2. disable allocation (clear SWP_WRITEOK)
3. try_to_unuse(type)

   → migrate all pages from swap back into memory

4. destroy extents
5. free swap_map
6. cleanup device
*/


/*
Meaning:

    evacuate swap device completely
*/


/***************************************************************
 * 10. try_to_unuse() (EXTREMELY IMPORTANT)
 ***************************************************************/

/*
Goal:

    remove ALL references to swap entries
*/


/*
Flow:

for each used swap entry:

    page = read_swap_cache_async(entry)

    for each process (mm):
        find PTE using this entry
        replace with real page

    delete_from_swap_cache()
*/


/*
Meaning:

    reverse of swapping → bring everything back
*/


/***************************************************************
 * 11. EXTENTS (VERY IMPORTANT)
 ***************************************************************/

/*
Swap file may not be contiguous on disk
*/


/*
struct swap_extent:

    start_page
    nr_pages
    start_block
*/


/*
Mapping:

    swap offset → disk block
*/


/*
Function:

    map_swap_page()
*/


/*
Meaning:

    logical → physical translation
*/


/***************************************************************
 * 12. setup_swap_extents()
 ***************************************************************/

/*
Build extent list for swap file
*/


/*
Flow:

walk file blocks via bmap()
    group contiguous blocks
    create extents
*/


/***************************************************************
 * 13. valid_swaphandles()
 ***************************************************************/

/*
Used for swap readahead
*/


/*
Returns contiguous valid swap slots
*/


/***************************************************************
 * 14. FULL FLOW (END-TO-END)
 ***************************************************************/

/*
SWAP OUT:

vmscan selects page
    ↓
get_swap_page()   (swapfile.c)
    ↓
add_to_swap_cache() (swap_state.c)
    ↓
swap_writepage() (page_io.c)

SWAP IN:

fault
    ↓
lookup_swap_cache()
    ↓
read_swap_cache_async()
    ↓
swap_readpage()

SWAPOFF:

sys_swapoff()
    ↓
try_to_unuse()
    ↓
restore all pages
*/


/***************************************************************
 * 15. KEY INSIGHT (VERY IMPORTANT)
 ***************************************************************/

/*
swapfile.c manages:

    WHERE swapped pages live on disk

swap_state.c manages:

    WHICH page corresponds to a swap entry
*/


/***************************************************************
 * 16. INTERVIEW MODEL
 ***************************************************************/

/*
mm/swapfile.c implements swap device management, including allocation
of swap slots, reference counting of swap entries, and the swapon/swapoff
system calls. It maintains swap_map[] for tracking usage and uses extent
lists to map logical swap offsets to physical disk blocks.
*/


/***************************************************************
 * 17. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/swapfile.c manages swap devices and swap slot allocation, tracking
usage via swap_map and enabling swapping through swapon/swapoff.
*/


/***************************************************************
 * END
 ***************************************************************/
