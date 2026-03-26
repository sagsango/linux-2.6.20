/*
 * Linux 2.6.20 — mm/sparse.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (CRITICAL concept: SPARSEMEM)
 *  - mem_map layout problem and solution
 *  - section-based memory model
 *  - NUMA interaction
 *  - mem_section and section_mem_map encoding
 *  - boot-time vs runtime behavior
 *  - memory hotplug support
 *
 * Source: user-provided sparse.c
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL — MUST UNDERSTAND)
 ***************************************************************/

/*
Problem:

    Kernel needs a struct page for EVERY physical page.

Classic model:

    mem_map[] = flat array indexed by PFN

But this breaks when:

    - physical memory is sparse (holes)
    - NUMA systems
    - memory hotplug

Solution:

    SPARSEMEM model

        → divide memory into SECTIONS
        → allocate mem_map only for PRESENT sections
*/


/***************************************************************
 * 1. WHY SPARSEMEM EXISTS
 ***************************************************************/

/*
Flat mem_map (old model):

    mem_map[pfn]

Problems:

1. Large address space with holes:
       waste memory (huge unused mem_map)

2. NUMA:
       need node-aware mapping

3. Memory hotplug:
       cannot easily grow mem_map dynamically

---------------------------------------

SPARSEMEM solves this:

    PFN → SECTION → mem_section → mem_map
*/


/***************************************************************
 * 2. CORE IDEA
 ***************************************************************/

/*
Physical memory is divided into:

    SECTIONS (fixed-size chunks)

Each section:

    represents a contiguous PFN range

Only PRESENT sections have mem_map allocated
*/


/***************************************************************
 * 3. CORE DATA STRUCTURE
 ***************************************************************/

/*
struct mem_section {
    unsigned long section_mem_map;
};
*/


/*
Global array:

    mem_section[NR_SECTION_ROOTS][SECTIONS_PER_ROOT]

or in EXTREME mode:

    mem_section[root] = dynamically allocated root array
*/


/*
Meaning:

mem_section:
    metadata per section

section_mem_map:
    encoded value:
        - pointer to struct page array (mem_map)
        - OR early boot nid encoding
        - OR flags
*/


/***************************************************************
 * 4. SECTION MODEL
 ***************************************************************/

/*
PFN → section_nr

section_nr = pfn / PAGES_PER_SECTION
*/


/*
Mapping chain:

PFN →
    mem_section →
        section_mem_map →
            struct page array
*/


/***************************************************************
 * 5. EARLY BOOT TRICK (VERY IMPORTANT)
 ***************************************************************/

/*
During early boot:

    section_mem_map DOES NOT point to mem_map

Instead:

    it stores NUMA node id in encoded form
*/


/*
Encoding helpers:

    sparse_encode_early_nid(nid)
    sparse_early_nid(section)
*/


/*
Why?

Reuse same field for:

    early nid info
    later mem_map pointer

→ saves memory + avoids extra structures
*/


/***************************************************************
 * 6. memory_present() (VERY IMPORTANT)
 ***************************************************************/

/*
Function:

    memory_present(nid, start, end)

Purpose:

    mark which PFN ranges are PRESENT on which node
*/


/*
Flow:

for each section in [start, end):
    sparse_index_init(section, nid)
    mark section as PRESENT
    store early nid encoding if not already initialized
*/


/*
Insight:

This builds the physical memory topology
before actual mem_map allocation happens.
*/


/***************************************************************
 * 7. ROOT/INDEX INITIALIZATION
 ***************************************************************/

/*
CONFIG_SPARSEMEM_EXTREME case:

    roots are allocated lazily

Functions:

    sparse_index_alloc(nid)
    sparse_index_init(section_nr, nid)
*/


/*
Meaning:

Instead of having one huge static mem_section array, the kernel can
allocate root chunks on demand.

This is useful when the section space itself is sparse.
*/


/***************************************************************
 * 8. page_to_nid() SUPPORT
 ***************************************************************/

/*
If node id is NOT stored in page flags:

    section_to_node_table[section_nr]

is used.

Then:

    page_to_nid(page)
        → page_to_section(page)
        → section_to_node_table[section]
*/


/*
Meaning:

SPARSEMEM also provides a way to recover node information for a page.
*/


/***************************************************************
 * 9. mem_map ALLOCATION
 ***************************************************************/

/*
Key function:

    sparse_early_mem_map_alloc(pnum)

Purpose:

    allocate struct page[] for one section
*/


/*
Flow:

1. determine nid from early-encoded section data
2. try alloc_remap(nid, size)
3. else try alloc_bootmem_node(node, size)
4. if both fail:
       warn and clear section mapping
*/


/*
Size:

    sizeof(struct page) * PAGES_PER_SECTION
*/


/***************************************************************
 * 10. ENCODING mem_map POINTER
 ***************************************************************/

/*
Helper:

    sparse_encode_mem_map(mem_map, pnum)

This encodes the real mem_map pointer in a compact way.
*/


/*
Key trick:

    encoded = mem_map - section_base_pfn

So later the real pointer can be reconstructed relative to the section.

This is a compact representation used inside section_mem_map.
*/


/***************************************************************
 * 11. sparse_init_one_section()
 ***************************************************************/

/*
Purpose:

    install real mem_map into a valid section

Flow:

1. check section is valid
2. clear old map bits
3. write encoded mem_map into section_mem_map
*/


/***************************************************************
 * 12. sparse_init() (BOOT TIME)
 ***************************************************************/

/*
Function:

    sparse_init()

Flow:

for each section number:
    if section valid:
        allocate mem_map for that section
        attach it via sparse_init_one_section()
*/


/*
Meaning:

This converts:

    early nid encoding

into:

    real mem_map pointer encoding
*/


/***************************************************************
 * 13. node_memmap_size_bytes()
 ***************************************************************/

/*
Purpose:

    compute how many bytes of mem_map a node needs

Used mainly by early NUMA setup.

Flow:

for each section PFN in [start_pfn, end_pfn):
    if early_pfn_to_nid(pfn) matches nid and pfn is valid:
        add PAGES_PER_SECTION

return nr_pages * sizeof(struct page)
*/


/***************************************************************
 * 14. RUNTIME SECTION ADDITION (HOTPLUG)
 ***************************************************************/

/*
Function:

    sparse_add_one_section(zone, start_pfn, nr_pages)

Purpose:

    add a new section at runtime
*/


/*
Flow:

1. determine section_nr from start_pfn
2. ensure index/root exists
3. allocate mem_map (__kmalloc_section_memmap)
4. take pgdat resize lock
5. mark section PRESENT
6. install mem_map encoding
7. unlock
8. if failed, free mem_map
*/


/*
This is the hotplug-facing path.

Key point:

    boot-time sparse_init() handles initial sections
    sparse_add_one_section() handles later additions
*/


/***************************************************************
 * 15. mem_map ALLOCATION STRATEGY FOR HOTPLUG
 ***************************************************************/

/*
Function:

    __kmalloc_section_memmap(nr_pages)

Flow:

1. compute memmap_size = sizeof(struct page) * nr_pages
2. try alloc_pages(order)
3. else try vmalloc(memmap_size)
4. zero the memory
5. return pointer
*/


/*
Meaning:

Hotplug path allows either:

    physically contiguous mem_map backing
or
    virtually contiguous backing via vmalloc

This is more flexible than early boot allocation.
*/


/***************************************************************
 * 16. FREEING mem_map
 ***************************************************************/

/*
Function:

    __kfree_section_memmap(memmap, nr_pages)

Flow:

if memmap is in vmalloc area:
    vfree(memmap)
else:
    free_pages(memmap, order)
*/


/*
Meaning:

The freeing path mirrors the dual allocation strategy.
*/


/***************************************************************
 * 17. __section_nr() REVERSE LOOKUP
 ***************************************************************/

/*
Function:

    __section_nr(struct mem_section *ms)

Purpose:

    given a mem_section pointer, compute section number

Flow:

scan roots:
    find which root array contains ms
    compute index within root
*/


/*
This is mainly helper/reverse-mapping logic.
*/


/***************************************************************
 * 18. VALID_SECTION / SECTION FLAGS MENTAL MODEL
 ***************************************************************/

/*
A section is meaningful only if marked present/valid.

Typical conceptual states:

    absent section
        -> no memory here

    present section
        -> memory exists here
        -> mem_map may be allocated/installed
*/


/*
Flags are packed into section_mem_map alongside encoding,
which is why sparse_init_one_section() masks and rewrites carefully.
*/


/***************************************************************
 * 19. CRITICAL CONCEPT: mem_map IS NOT ONE BIG FLAT ARRAY
 ***************************************************************/

/*
In SPARSEMEM:

    mem_map is NOT globally contiguous

Instead:

    each present section has its own struct page array
*/


/*
Visualization:

Flat model:

    mem_map[0 ... max_pfn]

Sparse model:

    section0  -> mem_map[]
    section1  -> NULL (hole)
    section2  -> mem_map[]
    section3  -> NULL (hole)
    section4  -> mem_map[]
*/


/***************************************************************
 * 20. WHY THIS SAVES MEMORY
 ***************************************************************/

/*
If physical address space has holes, a flat mem_map wastes memory because
it still reserves struct page descriptors for absent PFNs.

SPARSEMEM saves memory by allocating struct page arrays only for sections
that are marked present.
*/


/***************************************************************
 * 21. WHY THIS HELPS NUMA
 ***************************************************************/

/*
NUMA systems care about which node owns which memory ranges.

SPARSEMEM integrates naturally with NUMA because:

    - sections can be tagged with nid early
    - mem_map for a section can be allocated from that node
    - page_to_nid() can recover node via section info if needed
*/


/***************************************************************
 * 22. WHY THIS HELPS HOTPLUG
 ***************************************************************/

/*
Memory hotplug means memory can appear after boot.

A flat monolithic mem_map is awkward to grow dynamically.

Section-based model makes this easier:

    add one section
        -> allocate its mem_map
        -> mark present
        -> install mapping
*/


/***************************************************************
 * 23. FULL FLOW (END-TO-END)
 ***************************************************************/

/*
BOOT-TIME FLOW:

memory_present(nid, start, end)
    ↓
record present sections + early nid info
    ↓
sparse_init()
    ↓
for each valid section:
    allocate mem_map
    install encoded pointer

RUNTIME/HOTPLUG FLOW:

sparse_add_one_section(zone, start_pfn, nr_pages)
    ↓
ensure root/index exists
    ↓
allocate mem_map
    ↓
mark section present
    ↓
install encoded mem_map
*/


/***************************************************************
 * 24. PRACTICAL LOOKUP CHAIN YOU SHOULD REMEMBER
 ***************************************************************/

/*
PFN lookup mental model:

    pfn
      ↓
    section_nr
      ↓
    mem_section
      ↓
    encoded section_mem_map
      ↓
    struct page * for that section
      ↓
    page descriptor for exact PFN
*/


/***************************************************************
 * 25. CONNECTION TO OTHER MM FILES
 ***************************************************************/

/*
This file connects to almost everything in MM because struct page is the
central descriptor used everywhere.

Examples:

page allocator (buddy):
    allocates/frees pages represented by struct page

slab/slob:
    build object allocators on top of pages / struct page

page cache:
    cached file pages are struct page objects

rmap:
    reverse mapping works on struct page

writeback/readahead/swap:
    all operate on struct page
*/


/***************************************************************
 * 26. KEY INSIGHT (VERY IMPORTANT)
 ***************************************************************/

/*
struct page is the kernel's universal physical-page descriptor.

SPARSEMEM is fundamentally about:

    how the kernel locates struct page for a PFN

in a world where memory is not dense/flat.
*/


/***************************************************************
 * 27. INTERVIEW MENTAL MODEL
 ***************************************************************/

/*
If asked:

"What does mm/sparse.c do?"

Good answer:

    It implements the SPARSEMEM memory model. Instead of one flat mem_map
    array covering all PFNs, physical memory is divided into sections.
    Each present section gets its own struct page array, and absent sections
    have no mem_map allocated. This saves memory in sparse physical address
    spaces and supports NUMA-aware setup and memory hotplug.
*/


/***************************************************************
 * 28. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/sparse.c implements the SPARSEMEM memory model, dividing physical
memory into sections and allocating mem_map (struct page arrays) only
for present sections, enabling efficient handling of sparse memory,
NUMA, and memory hotplug.
*/


/***************************************************************
 * END
 ***************************************************************/
