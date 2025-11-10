# swap_state.c, swap.c & swapfile.c
swap_state.c — swap cache, swap cache lookup, add/remove
swap.c — core VM swap logic, LRU, accounting, reclaim glue
swapfile.c — swapon/swapoff, swap_map, scanning, disk extents


# High-Level Overview Flow
User anonymous memory
        │
        ▼
Page fault → alloc page → used → pressure
        │
        ▼
kswapd / direct reclaim picks victim
        │
        ▼
[ try_to_swap_out(page) ]
        │
        ▼
     add_to_swap()
        │
        ▼
┌──────────────────────────────────────┐
│        swap cache (swap_state.c)     │
└──────────────────────────────────────┘
        │   (radix-tree by swap entry)
        ▼
get_swap_page()  ← swap_map[][] (swapfile.c)
        │
        ▼
writepage → swap_writepage()
        │
        ▼
Disk swap area (file or blockdevice)


# Detailed Flow: Swapping OUT a Page
Victim page selected by vmscan
        │
        ▼
try_to_unmap() unmaps PTEs
        │
        ▼
try_to_swap_out(page)
        │
        ▼
add_to_swap(page)
│
├─► get_swap_page()                (swapfile.c)
│      │
│      ├─ scans swap_map bitmap
│      └─ allocates offset in swapfile
│
└─► add_to_swap_cache()
       │
       ├─ radix_tree_preload()
       ├─ radix_tree_insert()      (swapper_space.page_tree)
       ├─ SetPageSwapCache
       └─ private = entry.val
        ▼
Page now has SwapCache + swap entry
        ▼
swap_writepage()
        ▼
Block layer I/O → disk


# Detailed Flow: Swapping OUT a Page
                    SWAP-OUT PIPELINE
===================================================================
             (swap.c + swap_state.c + swapfile.c)
===================================================================

                  ┌─────────────────────────┐
                  │  vmscan selects victim  │
                  └────────────┬────────────┘
                               ▼
                   try_to_unmap(page)
                               │
                               ▼
                   try_to_swap_out(page)
                               │
                               ▼
                    add_to_swap(page)
                               │
        ┌─────────────────────────────────────────────────────────┐
        │                 get_swap_page() (swapfile.c)            │
        └─────────────────────────────────────────────────────────┘
             │
             ├── scan_swap_map(si)
             │       │
             │       ├── cluster allocation logic
             │       └── swap_map[offset] = 1   (mark used)
             │
             └── return swp_entry(type, offset)
                               │
                               ▼
                 add_to_swap_cache(page, entry)
                               │
        ┌─────────────────────────────────────────────────────────┐
        │                 swap_state.c logic                      │
        │                                                         │
        │  radix_tree_preload()                                   │
        │  write_lock(tree_lock)                                  │
        │  radix_tree_insert(page_tree, entry.val → page)         │
        │  SetPageSwapCache(page)                                 │
        │  page->private = entry.val                              │
        └─────────────────────────────────────────────────────────┘
                               │
                               ▼
                    swap_writepage(page)
                               │
                               ▼
                      Block I/O → Disk swap area

# Detailed Flow: Swap IN (page fault)
Page fault on a PTE containing swap entry
        │
        ▼
do_swap_page()
        │
        ▼
lookup_swap_cache(entry)
│      │
│      ├─ radix_tree_lookup()  ← swapper_space
│      └─ (hit? return page)
│
└── Miss (not in cache)
        │
        ▼
read_swap_cache_async(entry)
│
├── alloc_page_vma()
├── add_to_swap_cache(newpage)
└── swap_readpage(newpage)
        ▼
page is populated


# Flow Diagram: Swap-in Pipeline
                    SWAP-IN PIPELINE
====================================================================
                  (page fault → restored from swap)
====================================================================

User accesses swapped-out page
        │
        ▼
Page fault → do_swap_page()
        │
        ▼
Extract swp_entry from PTE
        │
        ▼
 lookup_swap_cache(entry)
        │
        ├── Hit → return page immediately
        │
        └── Miss → need disk I/O
                     ▼
        ┌───────────────────────────────────────────────┐
        │      read_swap_cache_async(entry)             │
        └───────────────────────────────────────────────┘
                     │
                     ├─ alloc_page_vma()
                     ├─ add_to_swap_cache(page)
                     └─ swap_readpage(page)
                                 │
                                 ▼
                 Disk → read page into memory
                                 │
                                 ▼
         install PTE + page_add_anon_rmap()


