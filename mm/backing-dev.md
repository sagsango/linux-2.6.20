================================================================================
FILE: backing_device_background_linux.txt
TOPIC: Background of backing_dev_info (BDI) in Linux Kernel
================================================================================


SECTION 1: THE PROBLEM LINUX NEEDED TO SOLVE
--------------------------------------------

Linux uses the PAGE CACHE to speed up file operations.

When a process writes to a file:

    write(fd, buffer, size)

the kernel usually does NOT immediately write the data to disk.

Instead the flow is:

    User write()
          │
          ▼
    Page cache page updated
          │
          ▼
    Page marked DIRTY
          │
          ▼
    Writeback later flushes page to disk


This improves performance significantly because disk I/O is slow.


However this introduces a major problem.



SECTION 2: DEVICES HAVE DIFFERENT SPEEDS
----------------------------------------

Example system:

    NVMe SSD       ~3 GB/s
    SATA HDD       ~200 MB/s
    USB drive      ~40 MB/s
    NFS network    unpredictable latency


If Linux allows processes to dirty pages without limits:

    Process writes 10GB extremely fast
    Disk can flush only 200MB/s

Result:

    RAM fills with dirty pages
    writeback queues explode
    system may stall


This situation is called:

    I/O CONGESTION


The kernel needs a mechanism to detect and control this.


Solution: Track per-device behavior.



SECTION 3: INTRODUCTION OF BACKING DEVICE INFO
----------------------------------------------

Linux introduced a structure:

    struct backing_dev_info


This structure represents the kernel's understanding of a
storage backend.


Examples:

    /dev/sda          -> BDI
    /dev/nvme0n1      -> BDI
    NFS mount         -> BDI
    tmpfs             -> BDI


Each filesystem or block device has a corresponding
backing_dev_info object.


Conceptually:

    BDI = Kernel representation of a storage backend



SECTION 4: WHERE BDI SITS IN THE KERNEL
---------------------------------------

The simplified architecture looks like:

        User Process
             │
             │ write()
             ▼
           VFS
             │
             ▼
         Page Cache
             │
             │ dirty page
             ▼
     backing_dev_info (BDI)
             │
             │ writeback control
             ▼
         Block Layer
             │
             ▼
       Device Driver
             │
             ▼
        Disk / SSD / Network FS


BDI acts as a control layer between the page cache
and the actual storage device.



SECTION 5: MAJOR RESPONSIBILITIES OF BDI
----------------------------------------


1) WRITEBACK CONTROL

BDI controls how dirty pages are flushed to disk.

Each BDI manages:

    writeback threads
    dirty limits
    congestion state


You can see BDIs in the system:

    /sys/class/bdi/



2) DIRTY PAGE THROTTLING

If a device is slow, Linux slows down writers.

Example logic:

    dirty pages exceed threshold
          │
          ▼
    balance_dirty_pages()
          │
          ▼
    kernel forces process to sleep

BDI helps determine these limits.



3) DEVICE CONGESTION TRACKING

BDI tracks when a device is overloaded.

Important flags:

    BDI_write_congested
    BDI_read_congested

Meaning:

    device I/O queues are full

Then kernel components back off.



4) WRITEBACK SCHEDULING

Each BDI manages writeback worker threads.

Typical thread names look like:

    flush-8:0
    flush-259:0

These threads flush dirty pages for specific devices.



SECTION 6: WHY BDI IS NECESSARY
-------------------------------

Without BDI the kernel would treat all devices the same.

Example scenario:

    Process writes heavily to slow USB disk.

Without BDI:

    USB device overloaded
    RAM fills with dirty pages
    entire system may stall

With BDI:

    USB device marked congested
    writers slowed down
    system remains stable



SECTION 7: EXAMPLE WORKFLOW
---------------------------

Consider this command:

    cp large_file /mnt/hdd


Flow:

    cp writes to page cache
          │
          ▼
    pages become DIRTY
          │
          ▼
    writeback thread flushes pages
          │
          ▼
    if disk queue full
          │
          ▼
    BDI_write_congested = 1
          │
          ▼
    writers sleep via congestion_wait()


When disk catches up:

    writeback completes
          │
          ▼
    clear_bdi_congested()
          │
          ▼
    wake up waiting writers



SECTION 8: IMPORTANT BDI STRUCTURE FIELDS
-----------------------------------------

Simplified example:

    struct backing_dev_info {

        unsigned long state;

        struct list_head bdi_list;

        struct writeback wb;

        unsigned int max_ratio;
        unsigned int min_ratio;

        unsigned long congested;

    };


Important field for congestion code:

    bdi->state


This contains flags such as:

    BDI_write_congested
    BDI_read_congested



SECTION 9: WHEN BDI OBJECTS ARE CREATED
---------------------------------------

BDI structures are created when:


1) BLOCK DEVICE IS REGISTERED

Example:

    /dev/sda


2) FILESYSTEM IS MOUNTED

Example:

    mount ext4 /dev/sda1


3) NETWORK FILESYSTEMS

Example:

    NFS
    Ceph
    SMB



SECTION 10: WHEN CONGESTION OCCURS
----------------------------------

Typical triggers include:


1) DISK QUEUE SATURATION

    Block layer request queue becomes full


2) EXCESSIVE DIRTY PAGES

    writeback cannot keep up with writers


3) NETWORK STORAGE LATENCY

    NFS server responding slowly


Kernel then marks the device:

    set_bdi_congested(bdi, WRITE)



SECTION 11: KERNEL COMPONENTS USING BDI
---------------------------------------

Multiple subsystems depend on BDI.


PAGE RECLAIM

    kswapd


DIRTY PAGE BALANCING

    balance_dirty_pages()


FILESYSTEMS

    ext4
    xfs
    btrfs


WRITEBACK SYSTEM

    fs/writeback.c



SECTION 12: HISTORICAL CONTEXT
------------------------------

BDI became important during the Linux 2.6 series.

Earlier kernels had simpler writeback systems.

Later kernels added:

    per-bdi writeback threads
    cgroup writeback
    blkcg integration


Modern kernels have more advanced mechanisms,
but BDI remains an important concept.



SECTION 13: SIMPLE MENTAL MODEL
-------------------------------

Think of BDI as a traffic controller for disk I/O.


    Processes  = cars
    Disk       = road
    BDI        = traffic lights


If road congested:

    red light
    cars wait


When road clears:

    green light
    cars move



SECTION 14: WHY THE CONGESTION CODE EXISTS
------------------------------------------

The code you showed implements congestion signaling.

Mechanism:

    device congested
        ↓
    set_bdi_congested()

    device free
        ↓
    clear_bdi_congested()

    tasks waiting
        ↓
    congestion_wait()


Purpose:

    avoid overwhelming slow devices
    prevent RAM exhaustion
    stabilize the writeback system



SECTION 15: FINAL SUMMARY
-------------------------

backing_dev_info is the kernel structure representing
a storage backend.

It manages:

    writeback
    dirty page throttling
    device congestion
    I/O scheduling coordination

This allows Linux to safely handle multiple storage
devices with very different performance characteristics.


================================================================================
END OF FILE
================================================================================
