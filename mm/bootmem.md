================================================================================
FILE: linux_bootmem_allocator_background_and_flow.txt
TOPIC: Boot-Time Physical Memory Allocator (bootmem.c)
KERNEL ERA: Linux 2.6.x
================================================================================


SECTION 1: WHY BOOTMEM EXISTS
================================================================================

During very early kernel boot, the normal memory allocator does not exist yet.

The buddy allocator requires:

    struct page array
    zone initialization
    memory management structures
    slab allocator

But those structures themselves require memory.

This creates a classic bootstrapping problem:

        We need memory to build the memory allocator.


Solution:

        A very simple early allocator called BOOTMEM.


Bootmem is a temporary physical memory allocator used only during
the early boot stage of the kernel.



SECTION 2: BOOT PROCESS MEMORY PROBLEM
================================================================================

At boot time the kernel knows only:

    physical memory layout from BIOS / firmware
    kernel image location
    reserved regions (ACPI, BIOS tables, etc)

Example memory layout:

    0x00000000 -------------------->
                BIOS data
    0x00100000 -------------------->
                Linux kernel image
    0x01000000 -------------------->
                free RAM
    0x7F000000 -------------------->
                device memory holes
    0x80000000 -------------------->
                more RAM


Kernel must:

    1) track which pages are usable
    2) allocate memory for kernel subsystems
    3) reserve special memory regions

Bootmem solves this problem.



SECTION 3: BOOTMEM DESIGN
================================================================================

Bootmem uses a very simple data structure:

        BITMAP of physical pages


Each page has a bit:

        1 = reserved
        0 = free


Example bitmap:

    Page:   0 1 2 3 4 5 6 7
    Bits:   1 1 1 0 0 0 1 1


Meaning:

    pages 0,1,2 reserved
    pages 3,4,5 free
    pages 6,7 reserved


The bitmap is stored in memory and tracks the allocation state
during early boot.



SECTION 4: MAIN BOOTMEM DATA STRUCTURE
================================================================================

bootmem_data_t


Important fields used in this file:

    node_bootmem_map

        pointer to bitmap


    node_boot_start

        physical start address of memory node


    node_low_pfn

        last page frame number of the node


    last_success

        last successful allocation


    last_pos / last_offset

        used to merge small allocations inside pages


Bootmem supports NUMA nodes, so each node has its own
bootmem_data_t.



SECTION 5: GLOBAL VARIABLES
================================================================================

max_low_pfn

    highest PFN in low memory


min_low_pfn

    first PFN in low memory


max_pfn

    maximum PFN in system


These values define the physical memory range
the kernel can manage.



SECTION 6: BOOTMEM INITIALIZATION FLOW
================================================================================

Bootmem initialization happens during early boot.

Simplified sequence:


    start_kernel()
        |
        v
    setup_arch()
        |
        v
    init_bootmem()
        |
        v
    reserve_bootmem()
        |
        v
    free_bootmem()
        |
        v
    bootmem allocations happen
        |
        v
    free_all_bootmem()
        |
        v
    buddy allocator takes over



Bootmem exists only until:

        free_all_bootmem()



SECTION 7: INITIALIZATION FUNCTION
================================================================================

init_bootmem_core()


Purpose:

    initialize bootmem bitmap


Steps performed:


1) store pointer to bitmap

    bdata->node_bootmem_map = phys_to_virt(PFN_PHYS(mapstart))


2) set start of memory region

    bdata->node_boot_start


3) set last PFN

    bdata->node_low_pfn


4) link node into bootmem list

    link_bootmem()


5) mark all pages reserved

    memset(bitmap, 0xff)


Important:

    Initially everything is RESERVED.


Later functions mark free pages.



SECTION 8: RESERVING MEMORY
================================================================================

reserve_bootmem_core()


Purpose:

    mark physical memory as RESERVED


Used for:

    kernel image
    BIOS tables
    ACPI
    device memory holes


Steps:


1) convert physical address to page index

    sidx = PFN_DOWN(addr - node_boot_start)


2) compute ending page

    eidx = PFN_UP(addr + size - node_boot_start)


3) set bits in bitmap

    test_and_set_bit()


Meaning:

    those pages cannot be allocated.



SECTION 9: FREEING MEMORY
================================================================================

free_bootmem_core()


Purpose:

    mark pages usable for allocation


Example:

    after architecture setup detects RAM areas


Steps:

    clear bits in bitmap


Result:

    those pages become allocatable.


Code:

    test_and_clear_bit()



SECTION 10: BOOTMEM ALLOCATION
================================================================================

Main allocator:

    __alloc_bootmem_core()


This function performs:


1) find contiguous free pages
2) respect alignment
3) optionally prefer addresses above "goal"
4) mark pages reserved
5) return virtual address


Core algorithm:

    scan bitmap
    find zero bits
    check if region large enough
    reserve bits
    return address



SECTION 11: SEARCH ALGORITHM
================================================================================

Allocator scans bitmap:


    find_next_zero_bit()


Meaning:

    find first free page


Then checks next pages:

    if contiguous free


If not free:

    skip to next aligned location



SECTION 12: PAGE MERGING OPTIMIZATION
================================================================================

Bootmem attempts to pack small allocations.

Example:


    allocation 1: 100 bytes
    allocation 2: 200 bytes


Instead of wasting two pages:

    both allocations share same page.


Variables used:

    last_pos
    last_offset


This reduces memory waste during boot.



SECTION 13: ALLOCATION RESULT
================================================================================

After finding pages:


1) mark bitmap bits reserved

    test_and_set_bit()


2) zero memory

    memset(ret,0,size)


3) return virtual address



SECTION 14: FREEING BOOTMEM
================================================================================

When the kernel memory system is ready:


    free_all_bootmem_core()


This converts all bootmem free pages into
normal buddy allocator pages.


Steps:


1) iterate bitmap

2) find free pages

3) call

    __free_pages_bootmem()


4) free bitmap itself


After this step:

    bootmem allocator disappears.



SECTION 15: NUMA SUPPORT
================================================================================

Multiple nodes exist in NUMA systems.


Each node has:

    pg_data_t
        |
        v
    bootmem_data_t


bootmem_data_t objects are stored in:


    bdata_list


Allocation functions iterate over nodes.



SECTION 16: WRAPPER FUNCTIONS
================================================================================

These wrappers call the core functions.


init_bootmem_node()

    initialize node bootmem


reserve_bootmem_node()

    reserve region


free_bootmem_node()

    free region


__alloc_bootmem()

    allocate boot memory


__alloc_bootmem_low()

    allocate memory under 4GB


__alloc_bootmem_node()

    allocate memory on specific NUMA node



SECTION 17: COMPLETE FLOW DIAGRAM
================================================================================

Early boot memory management flow:


BIOS / Firmware
        |
        v
Detect physical memory
        |
        v
setup_arch()
        |
        v
init_bootmem()
        |
        v
create bitmap of pages
        |
        v
mark all pages RESERVED
        |
        v
free_bootmem() marks usable RAM
        |
        v
bootmem allocations happen
        |
        v
kernel subsystems initialized
        |
        v
free_all_bootmem()
        |
        v
buddy allocator activated
        |
        v
bootmem allocator destroyed



SECTION 18: WHY BOOTMEM IS SIMPLE
================================================================================

Bootmem is intentionally simple.


It:

    uses bitmap
    scans linearly
    does not support freeing individual allocations
    used only during boot


Advantages:

    extremely reliable
    simple
    minimal dependencies


Disadvantages:

    slow
    cannot free individual allocations


But performance does not matter because
it runs only once during boot.



SECTION 19: MODERN KERNEL NOTE
================================================================================

Modern kernels replaced bootmem with:

        memblock allocator


memblock is more flexible and supports
dynamic reservations.


But bootmem.c was the original early
Linux memory allocator.



SECTION 20: SUMMARY
================================================================================

bootmem allocator provides early boot memory allocation.

It works by:

    maintaining bitmap of pages
    marking reserved/free memory
    scanning bitmap for contiguous free pages
    allocating memory during boot
    later releasing all memory to buddy allocator


This allows Linux to bootstrap its full
memory management system.


================================================================================
END OF FILE
================================================================================
