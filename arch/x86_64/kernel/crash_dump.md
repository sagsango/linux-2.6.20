```text
===============================================================================
CRASH DUMP OLD MEMORY COPY
File: kernel/crash_dump.c
===============================================================================

PURPOSE
=======

This file supports crash dump / kdump.

When the first kernel crashes, Linux boots into a second small kernel
called the crash kernel.

The crash kernel's job is to read memory from the crashed kernel and
save it into a vmcore file.

That old crashed kernel memory is called:

    oldmem


===============================================================================
BACKGROUND: NORMAL BOOT VS KDUMP BOOT
===============================================================================

Normal boot:

    Firmware
       |
       v
    Linux kernel
       |
       v
    System running


Kdump boot:

    First kernel running
       |
       v
    Kernel crash / panic
       |
       v
    kexec into crash kernel
       |
       v
    Crash kernel boots
       |
       v
    Crash kernel reads old memory
       |
       v
    Save /proc/vmcore


===============================================================================
WHY OLD MEMORY IS SPECIAL
===============================================================================

In the crash kernel:

    Current kernel memory = crash kernel memory

    Old kernel memory     = crashed kernel memory


The crash kernel does not have normal page table mappings for all old memory.

So it cannot simply do:

    memcpy(buf, old_kernel_address, size)

Because old memory is just physical memory from the crashed kernel.

The crash kernel must temporarily map it.


===============================================================================
MAIN FUNCTION
===============================================================================

Function:

    copy_oldmem_page()

Prototype:

    ssize_t copy_oldmem_page(
        unsigned long pfn,
        char *buf,
        size_t csize,
        unsigned long offset,
        int userbuf
    )


Purpose:

    Copy bytes from one physical page of old kernel memory.


Arguments:

    pfn
        Page Frame Number of old memory page.

    buf
        Destination buffer.

    csize
        Number of bytes to copy.

    offset
        Offset inside the page.

    userbuf
        If 1, destination is userspace.
        If 0, destination is kernel space.


===============================================================================
WHAT IS PFN?
===============================================================================

PFN = Page Frame Number

Physical address:

    physical_address = pfn << PAGE_SHIFT


If:

    PAGE_SHIFT = 12

Then:

    PAGE_SIZE = 4096

Example:

    pfn = 0x100
    physical address = 0x100000


Diagram:

Physical Memory:

+---------------------+
| PFN 0               |
+---------------------+
| PFN 1               |
+---------------------+
| PFN 2               |
+---------------------+
| ...                 |
+---------------------+
| PFN N               |
+---------------------+


===============================================================================
WHY IOREMAP?
===============================================================================

Code:

    vaddr = ioremap(pfn << PAGE_SHIFT, PAGE_SIZE);


ioremap() creates a temporary kernel virtual mapping for a physical address.

Input:

    old physical page

Output:

    temporary kernel virtual address


Flow:

Old Physical Memory Page
        |
        v
ioremap()
        |
        v
Temporary Kernel Virtual Mapping
        |
        v
memcpy / copy_to_user


Before ioremap:

    old physical page
          |
          X
    no kernel virtual mapping


After ioremap:

    vaddr
      |
      v
    old physical page


===============================================================================
FUNCTION FLOW
===============================================================================

copy_oldmem_page()
      |
      +--> if csize == 0
      |        |
      |        +--> return 0
      |
      +--> physical address = pfn << PAGE_SHIFT
      |
      +--> vaddr = ioremap(physical address, PAGE_SIZE)
      |
      +--> if destination is userspace
      |        |
      |        +--> copy_to_user(buf, vaddr + offset, csize)
      |
      +--> else destination is kernel
      |        |
      |        +--> memcpy(buf, vaddr + offset, csize)
      |
      +--> iounmap(vaddr)
      |
      +--> return copied size


===============================================================================
USER BUFFER CASE
===============================================================================

Code:

    if (userbuf) {
        if (copy_to_user(buf, vaddr + offset, csize)) {
            iounmap(vaddr);
            return -EFAULT;
        }
    }


Meaning:

Destination buffer belongs to userspace.

Example:

    cat /proc/vmcore > vmcore


Userspace process reads /proc/vmcore.

Kernel copies old memory into userspace buffer.

Use:

    copy_to_user()

because userspace pointer must be validated safely.


Flow:

Old Physical Page
      |
      v
Temporary Kernel Mapping
      |
      v
copy_to_user()
      |
      v
Userspace Buffer


If copy_to_user fails:

    unmap temporary mapping
    return -EFAULT


===============================================================================
KERNEL BUFFER CASE
===============================================================================

Code:

    memcpy(buf, vaddr + offset, csize);


Meaning:

Destination buffer is kernel memory.

No userspace access checks needed.


Flow:

Old Physical Page
      |
      v
Temporary Kernel Mapping
      |
      v
memcpy()
      |
      v
Kernel Buffer


===============================================================================
WHY IONUNMAP?
===============================================================================

Code:

    iounmap(vaddr);

The mapping is temporary.

After copying, remove it.

Otherwise:

    - virtual address space leaks
    - stale mappings remain
    - memory management becomes unsafe


===============================================================================
OFFSET AND CSIZE
===============================================================================

The function copies part of one page.

Example:

    PAGE_SIZE = 4096
    offset    = 100
    csize     = 200

Copy range:

    physical page + 100
          |
          v
    200 bytes


Diagram:

One old page:

+------------------------------------------------+
| byte 0                                         |
| ...                                            |
| byte 100  <---- copy starts                    |
| byte 101                                      |
| ...                                            |
| byte 299  <---- copy ends                      |
| ...                                            |
| byte 4095                                      |
+------------------------------------------------+


===============================================================================
WHERE THIS IS USED
===============================================================================

Commonly used by:

    /proc/vmcore

When crash kernel exposes old kernel memory as an ELF core file.

Userspace tools:

    makedumpfile
    crash
    gdb

can read /proc/vmcore to analyze the crashed kernel.


===============================================================================
KDUMP COMPLETE FLOW
===============================================================================

First Kernel
      |
      v
Kernel Panic
      |
      v
kexec into Crash Kernel
      |
      v
Crash Kernel Boots
      |
      v
/proc/vmcore Created
      |
      v
User reads /proc/vmcore
      |
      v
copy_oldmem_page()
      |
      +--> map old physical page
      |
      +--> copy bytes
      |
      +--> unmap page
      |
      v
vmcore saved


===============================================================================
WHY THIS FILE IS SMALL BUT IMPORTANT
===============================================================================

The crash kernel must not disturb old memory too much.

It needs to inspect memory from the crashed kernel safely.

So this function carefully does:

    temporary map
    copy
    unmap

one page at a time.


===============================================================================
KEY IDEA
===============================================================================

copy_oldmem_page() is the bridge between:

    Crash kernel virtual memory

and

    Crashed kernel physical memory


It lets the new crash kernel read the previous kernel's RAM page by page,
so the system can save a crash dump for debugging.
===============================================================================
```

