================================================================================
               LINUX 2.6.20 — SHARED PAGE SWAP FLOW (ASCII)
================================================================================

LEGEND:
  VA  = Virtual Address
  PA  = Physical Address (RAM frame)
  PTE = Page Table Entry
  SWP = Swap Entry (disk slot)
  rmap = Reverse Mapping
  VMA = Virtual Memory Area

--------------------------------------------------------------------------------
1) MULTIPLE PROCESSES SHARE ONE PHYSICAL PAGE
--------------------------------------------------------------------------------

Process A                Process B                Process C
----------               ----------               ----------
VA 0x4000                VA 0x9000                VA 0x7000
   |                        |                        |
   v                        v                        v
+--------+              +--------+              +--------+
|  PTE A |              |  PTE B |              |  PTE C |
+--------+              +--------+              +--------+
      \                      |                       /
       \                     |                      /
        \                    |                     /
         v                   v                    v
                   +----------------------+
                   |   struct page P     |
                   |----------------------|
                   | PA = 0x12345000     |
                   | _mapcount = 3       |
                   | _count = N          |
                   +----------------------+

--------------------------------------------------------------------------------
2) MEMORY PRESSURE → PAGE RECLAIM STARTS
--------------------------------------------------------------------------------

kswapd / direct reclaim
        |
        v
mm/vmscan.c : shrink_page_list(page)

Check page:
  - PageSwapBacked? YES
  - PageLocked? NO
  - page_mapcount(page) > 1 ? YES  --> shared page

--------------------------------------------------------------------------------
3) REVERSE MAPPING (RMAP) — FIND ALL PTEs
--------------------------------------------------------------------------------

mm/rmap.c : try_to_unmap(page)

Kernel walks anon_vma chain:

           struct anon_vma
                  |
                  v
        +------------------------+
        | VMA (Process A)        |
        | VMA (Process B)        |
        | VMA (Process C)        |
        +------------------------+
                  |
                  v
        Find PTEs mapping page P


--------------------------------------------------------------------------------
4) UNMAP ALL PTEs
--------------------------------------------------------------------------------

Before:
  PTE(A) -> PA 0x12345000
  PTE(B) -> PA 0x12345000
  PTE(C) -> PA 0x12345000

Kernel clears PTEs:

  ptep_get_and_clear(PTE(A))
  ptep_get_and_clear(PTE(B))
  ptep_get_and_clear(PTE(C))

--------------------------------------------------------------------------------
5) CREATE SWAP ENTRY
--------------------------------------------------------------------------------

mm/swap_state.c

swap_entry = get_swap_page()

Example:
  SWAP SLOT = S42

--------------------------------------------------------------------------------
6) UPDATE ALL PTEs TO POINT TO SAME SWAP ENTRY
--------------------------------------------------------------------------------

After:
  PTE(A) -> SWAP(S42)
  PTE(B) -> SWAP(S42)
  PTE(C) -> SWAP(S42)

NOTE:
  >>> SHARING IS PRESERVED <<<

--------------------------------------------------------------------------------
7) WRITE PAGE TO DISK
--------------------------------------------------------------------------------

mm/page_io.c : swap_writepage(page)

RAM:
  Page P (PA 0x12345000)
      |
      v
Disk Swap Area:
  Slot S42

--------------------------------------------------------------------------------
8) FREE PHYSICAL FRAME
--------------------------------------------------------------------------------

__free_page(page)

RAM FRAME 0x12345000 is reclaimed.

--------------------------------------------------------------------------------
9) LATER: PROCESS B ACCESSES PAGE → PAGE FAULT
--------------------------------------------------------------------------------

Process B:
  access VA 0x9000
      |
      v
CPU MMU:
  PTE(B) = SWAP(S42) → #PF

--------------------------------------------------------------------------------
10) SWAP-IN (PAGE FAULT HANDLER)
--------------------------------------------------------------------------------

mm/memory.c : do_swap_page()

Kernel:
  - alloc new page P'
  - read disk slot S42 → P'
  - update PTE(B)

Now:

RAM:
  Page P' (new frame)
     ^
     |
   PTE(B)

Disk:
  Slot S42
     ^
     |
   PTE(A), PTE(C)

--------------------------------------------------------------------------------
11) COPY-ON-WRITE (IF PROCESS WRITES)
--------------------------------------------------------------------------------

If Process B writes:

Kernel:
  - alloc page P''
  - copy P' → P''
  - update PTE(B)

Final state:

Process A -> SWAP(S42)
Process C -> SWAP(S42)
Process B -> PA(P'')

--------------------------------------------------------------------------------
12) KEY KERNEL DATA STRUCTURES (2.6.20)
--------------------------------------------------------------------------------

struct page
  |
  +-- _mapcount  (number of PTEs)
  +-- anon_vma   (reverse mapping)
  +-- mapping    (file-backed pages)

anon_vma chain:
  page -> anon_vma -> VMA -> mm_struct -> PTE

--------------------------------------------------------------------------------
13) IMPORTANT KERNEL FUNCTIONS (2.6.20)
--------------------------------------------------------------------------------

mm/vmscan.c:
  shrink_page_list()

mm/rmap.c:
  try_to_unmap()
  page_remove_rmap()

mm/swap_state.c:
  get_swap_page()
  add_to_swap_cache()

mm/page_io.c:
  swap_writepage()

mm/memory.c:
  do_swap_page()
  handle_mm_fault()

--------------------------------------------------------------------------------
14) CORE PRINCIPLE
--------------------------------------------------------------------------------

CPU page tables map:
  VA -> PA

Kernel reverse mapping maps:
  PA -> { VA1, VA2, VA3 }

Swapping operates on:
  >>> PHYSICAL PAGE, NOT PROCESS <<<

================================================================================
END OF ASCII DIAGRAM
================================================================================

