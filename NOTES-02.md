================================================================================
                         PHYSICAL PAGE (struct page)
================================================================================

                           +----------------------+
                           |      struct page     |
                           |----------------------|
                           | flags                |
                           | _count               |
                           | _mapcount (PTEs-1)   |
                           | mapping  ------------+-------------------+
                           | index                |                   |
                           +----------------------+                   |
                                                                      |
                                                                      |
                      +-----------------------------------------------+
                      |
                      |  mapping pointer decides page type
                      |
         +------------+-------------------------------+
         |                                            |
         |                                            |
         v                                            v
================================================================================
(1) ANONYMOUS PAGE RMAP                     (2) FILE-BACKED PAGE RMAP
================================================================================

    page->mapping = anon_vma + flag         page->mapping = address_space*
    (PAGE_MAPPING_ANON bit set)            (inode / file cache)

    +-------------------+                 +------------------------+
    |    struct anon_vma|                 |   struct address_space |
    |-------------------|                 |------------------------|
    | spinlock_t lock   |                 | i_mmap_lock            |
    | list_head head ---+----+----+       | prio_tree_root i_mmap--+----+
    +-------------------+    |    |       +------------------------+    |
                             |    |                                       |
                             v    v                                       v
                        +---------+---------+                     +--------+--------+
                        |       VMA A       |                     |      VMA A       |
                        |-------------------|                     |------------------|
                        | vm_mm = mmA      |                     | vm_mm = mmA      |
                        | vm_start/end     |                     | vm_start/end     |
                        +---------+---------+                     +--------+--------+
                                  |                                           |
                                  v                                           v
                              page tables                                  page tables
                                  |                                           |
                                  v                                           v
                               PTE(A)                                       PTE(A)


                             v    v                                       v
                        +---------+---------+                     +--------+--------+
                        |       VMA B       |                     |      VMA B       |
                        |-------------------|                     |------------------|
                        | vm_mm = mmB      |                     | vm_mm = mmB      |
                        +---------+---------+                     +--------+--------+
                                  |                                           |
                                  v                                           v
                               page tables                                  page tables
                                  |                                           |
                                  v                                           v
                               PTE(B)                                       PTE(B)


                             v    v                                       v
                        +---------+---------+                     +--------+--------+
                        |       VMA C       |                     |      VMA C       |
                        |-------------------|                     |------------------|
                        | vm_mm = mmC      |                     | vm_mm = mmC      |
                        +---------+---------+                     +--------+--------+
                                  |                                           |
                                  v                                           v
                               page tables                                  page tables
                                  |                                           |
                                  v                                           v
                               PTE(C)                                       PTE(C)

================================================================================
# From VMA to page VA:
    vma->vm_start + (page->index - vma->vm_pgoff) * PAGE_SIZE;
    so page index is just relative index within a vma

# Full reverse mapping logic
struct page P
   |
   v
find anon_vma or address_space
   |
   v
iterate all VMAs
   |
   v
for each VMA:
   |
   +--> compute virtual address using page->index + vm_pgoff
   |
   +--> walk page tables
   |
   +--> if PTE maps this page → found mapping

