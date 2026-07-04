/*
FILE: genhd.c.notes
SHORT IDE NOTES

1. PURPOSE
- Generic disk (gendisk) management layer.
- Bridges block drivers and VFS/block layer.
- Manages disk registration, major numbers, partitions, sysfs.

2. MAIN OBJECTS

gendisk
    Represents one block disk.

hd_struct
    Represents one partition.

block_subsys
    Global block subsystem.

bdev_map
    Maps device numbers -> gendisk.

3. DRIVER FLOW

alloc_disk()
   |
fill gendisk fields
   |
add_disk()
   |
register region
register disk
register queue
   |
disk visible to kernel/userspace

4. MAJOR REGISTRATION

register_blkdev()
    Allocate/register major number.

unregister_blkdev()
    Remove registration.

5. DEVICE LOOKUP

dev_t
  |
get_gendisk()
  |
kobj_lookup()
  |
gendisk

6. add_disk()

Marks disk UP.
Registers device numbers.
Creates sysfs entries.
Makes partitions discoverable.

7. alloc_disk()

Allocates:
 - gendisk
 - partition array
 - disk statistics
 - kobject

8. SYSFS

Attributes:
 dev
 size
 range
 removable
 stat
 uevent

Visible under /sys/block/.

9. PROC

/proc/partitions
/proc/diskstats

Generated using seq_file iterators.

10. UEVENTS

block_uevent()

Exports:
 MAJOR
 MINOR
 PHYSDEVPATH
 PHYSDEVBUS
 PHYSDEVDRIVER

Used by udev.

11. READ-ONLY

set_disk_ro()
set_device_ro()
bdev_read_only()

Control/query read-only policy.

12. COMPLETE FLOW

Driver
 |
alloc_disk()
 |
register_blkdev()
 |
add_disk()
 |
/sys/block
/proc/partitions
device nodes
 |
I/O requests

13. MENTAL MODEL

gendisk.c is the central disk management layer.

It creates, registers, exposes and destroys block disks while connecting
drivers to the Linux block subsystem.

One-line summary:

gendisk.c manages the lifetime and visibility of block disks, including
major numbers, gendisk objects, partitions, sysfs, procfs and device lookup.

