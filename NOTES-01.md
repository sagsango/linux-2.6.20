# ext2 disk layout
+----------------------------- DISK BEGIN --------------------------------------+
| Block 0 : Boot Sector (not used by ext2, except for bootloader)               |
+--------------------------------------------------------------------------------
| Block 1 : Superblock (MAIN)                                                   |
+--------------------------------------------------------------------------------
| Block 2..N : Block Group Descriptors                                          |
+--------------------------------------------------------------------------------
|
|                                 BLOCK GROUP 0
|--------------------------------------------------------------------------------
| Superblock Copy (group 0 always has it)                                       |
| Group Descriptor Table Copy                                                   |
| Block Bitmap                                                                  |
| Inode Bitmap                                                                  |
| Inode Table                                                                   |
| Data Blocks                                                                   |
|
+--------------------------------------------------------------------------------
|                                 BLOCK GROUP 1
|--------------------------------------------------------------------------------
| Superblock Copy? (only if sparse_super)                                       |
| Group Descriptor Table Copy?                                                  |
| Block Bitmap                                                                  |
| Inode Bitmap                                                                  |
| Inode Table                                                                   |
| Data Blocks                                                                   |
|
+--------------------------------------------------------------------------------
|                                 BLOCK GROUP 2
|--------------------------------------------------------------------------------
| (Maybe no superblock copy)                                                    |
| Block Bitmap                                                                  |
| Inode Bitmap                                                                  |
| Inode Table                                                                   |
| Data Blocks                                                                   |
|
+--------------------------------------------------------------------------------
|                                        ...                                    |
+--------------------------------------------------------------------------------
|                                 BLOCK GROUP N
|--------------------------------------------------------------------------------
| Block Bitmap                                                                  |
| Inode Bitmap                                                                  |
| Inode Table                                                                   |
| Data Blocks                                                                   |
+------------------------------ DISK END ----------------------------------------+

# ext2 disk layout (more detailed)
+----------------------------- DISK START ---------------------------------------+
|                                                                               |
| BLOCK 0 : BOOT BLOCK                                                          |
|   - Reserved for bootloader code (e.g., GRUB stage 1)                          |
|   - EXT2 does NOT store filesystem data here                                   |
|                                                                               |
+--------------------------------------------------------------------------------
| BLOCK 1 : SUPERBLOCK (MAIN COPY)                                              |
|   Contains:                                                                    |
|     - total number of inodes                                                   |
|     - total number of blocks                                                   |
|     - block size, fragment size                                                |
|     - blocks per group, inodes per group                                       |
|     - mount counts, last mount time                                            |
|     - compatible/incompatible features                                         |
|     - magic signature (0xEF53)                                                 |
|                                                                               |
+--------------------------------------------------------------------------------
| BLOCKS 2..N : GROUP DESCRIPTOR TABLE                                           |
|   Array of `ext2_group_desc` entries:                                          |
|     struct ext2_group_desc {                                                   |
|        block_bitmap;       // block number                                     |
|        inode_bitmap;       // block number                                     |
|        inode_table;        // starting block                                    |
|        free_blocks_count;                                                      |
|        free_inodes_count;                                                      |
|        used_dirs_count;                                                        |
|     };                                                                         |
|                                                                               |
+--------------------------------------------------------------------------------
|
|                              BLOCK GROUP 0                                     |
| (Starts immediately after GDT; location depends on block size)                 |
|--------------------------------------------------------------------------------|
| Superblock COPY (always appears in BG 0)                                       |
| Group Descriptor Table COPY (always appears in BG 0)                            |
| Block Bitmap (1 bit per block *in this group*)                                 |
| Inode Bitmap (1 bit per inode *in this group*)                                 |
| Inode Table (array of inode structures)                                        |
| Data Blocks (files, dirs, indirect blocks, symlinks)                            |
|                                                                                |
+--------------------------------------------------------------------------------+
|                              BLOCK GROUP 1                                     |
|--------------------------------------------------------------------------------|
| Superblock COPY? (only if sparse_super says so)                                 |
| Group Descriptor TABLE copy?                                                   |
| Block Bitmap                                                                    |
| Inode Bitmap                                                                    |
| Inode Table                                                                     |
| Data Blocks                                                                     |
+--------------------------------------------------------------------------------+
|                              BLOCK GROUP 2                                     |
|--------------------------------------------------------------------------------|
| Block Bitmap                                                                    |
| Inode Bitmap                                                                    |
| Inode Table                                                                     |
| Data Blocks                                                                     |
+--------------------------------------------------------------------------------+
|                                            ...                                 |
+--------------------------------------------------------------------------------+
|                              BLOCK GROUP N                                     |
|--------------------------------------------------------------------------------|
| Block Bitmap                                                                    |
| Inode Bitmap                                                                    |
| Inode Table                                                                     |
| Data Blocks                                                                     |
+------------------------------ DISK END ----------------------------------------+

# ASCII Diagram: COMPLETE EXT2 IMPLEMENTATION MAP (WITH DIRECTORIES) 
USER SPACE
   |
   v
sys_read / sys_write
   |
   v
+---------------------------+
|  VFS (mm/filemap)         |
|  fs/read_write.c          |
+---------------------------+
   |
   v
+---------------------------+
|       EXT2 FS             |
|       fs/ext2/            |
|  - inode.c                |
|  - dir.c                  |
|  - file.c                 |
|  - balloc.c, ialloc.c     |
+---------------------------+
   |
   v
+---------------------------+
|     Buffer/BIO Layer      |
|     fs/buffer.c           |
|     block/bio.c           |
+---------------------------+
   |
   v
+---------------------------+
|      Block Layer          |
|      block/               |
|  - ll_rw_blk.c            |
|  - genhd.c                |
|  - elevator.c (scheduler) |   XXX: Schedulers are in this layer
|  - schedulers (other)     |
+---------------------------+
   |
   v
+---------------------------+
|   Disk Driver (IDE/SCSI)  |   XXX: No scheduler
|   drivers/ide/ or         |
|   drivers/scsi/           |
|  - ide-disk.c             |
|  - ide-io.c               |
|  - scsi drivers           |
+---------------------------+
   |
   v
+---------------------------+
|     Hardware (HDD/SSD)    |
+---------------------------+
   |
   v
Interrupt (IRQ)
   |
   v
Driver IRQ handler
   |
   v
Block layer end_request
   |
   v
Page unlocked → data to user

# we can hook any driver
       +--------------------------------------+
       |             EXT2 filesystem          |
       +--------------------------------------+
                        |
                        v
            +---------------------------+
            |         block layer       |
            |  (request queue, BIO)     |
            +-------------+-------------+
                          |
                 chooses proper driver
                          |
        --------------------------------------------------------------------+
        |           |              |           |             |              |
        v           v              v           v             v              v
    drivers/ide  drivers/ata    drivers/scsi  drivers/usb  drivers/mmc     dma
    (IDE/PATA)   (SATA/libata)  (SCSI/SAS)    (USB disks)   (SD cards)      |
        |           |              |           |             |              |
        +-----------+--------------+-----------+-------------+--------------+
                        HARDWARE


# generic request_fn() to talk to drivers layer
   Filesystem (ext2/ext3 etc)
        |
        v
     submit_bh()
        |
        v
     submit_bio()
        |
        v
  +-------------------------------+
  |  generic_make_request(bio)    |
  |-------------------------------|
  | Check size                    |
  | Partition remap               |
  | Stacking drivers (dm/md)      |
  | bdev -> queue lookup          |
  | q->make_request_fn()          |
  +-------------------------------+
        |
        v
   Driver's request_fn()
        |
        v
   Hardware / DMA / Interrupts



generic_make_request(bio)
        |
        v
    Check device size
        |
        v
    do {
        |
        +--> Find block device queue
        |
        +--> Validate bio size w.r.t queue
        |
        +--> Partition remapping
        |         /dev/sda1 block X → /dev/sda block X+start
        |
        +--> Stacking drivers may rewrite:
        |         LVM, device mapper
        |         RAID0/RAID1/RAID5
        |         loop device
        |
        +--> q->make_request_fn(q, bio)
        |         |
        |         +--> For real hardware: __make_request()
        |         |
        |         +--> For stacking drivers: remap, split,
        |                                        clone, redirect
        |
    } while (ret == 1);
        |
        v
   BIO is now in the request queue of the final device


# ide disk
ext2 / submit_bh
        ↓
submit_bio()
        ↓
generic_make_request()
        ↓
__make_request()                 (block/ll_rw_blk.c)
        ↓
queue request inserted
        ↓
queue->request_fn = ide_do_request
        ↓
ide_do_request()                 (drivers/ide/ide.c)
        ↓
ide_start_request()
        ↓
program IDE controller registers
        ↓
start DMA or PIO transfer
        ↓
disk reads/writes sector
        ↓
IRQ from IDE controller
        ↓
ide_intr()                       (IDE interrupt handler)
        ↓
__blk_end_request()
        ↓
bio_endio()
        ↓
EXT2 receives page completion



+----------------------+
| generic_make_request |
+----------------------+
           |
           v
+----------------------+
|  request queue       |
| (IDE queue)          |
+----------------------+
           |
           v
+----------------------+
| queue->request_fn    |
|   = ide_do_request   |
+----------------------+
           |
           v
+----------------------------+
|  ide_do_request()          |
|  - takes next request      |
|  - sets up IDE registers   |
|  - starts DMA/PIO          |
+----------------------------+
           |
           v
+----------------------------+
|  IDE controller hardware   |
+----------------------------+
           |
           v
+----------------------------+
|  IDE interrupt (ide_intr)  |
+----------------------------+
           |
           v
+----------------------------+
|  end_that_request_first    |
|  end_that_request_last     |
|  bio_endio()               |
+----------------------------+


# elevator (schduler example)
   +---------------------+
   |   Request Queue     |
   +---------------------+
         |    |    |
  sorted v    |    | requeue
+------------+|    |
| sort_list  ||    |
+------------+|    |
              |    |
              v    |
+------------------+
| dispatch queue   |
+------------------+
       ^
       |
     front/back


submit_bio()
   ↓
generic_make_request()
   ↓
__make_request()
   ↓
Merge BIO into existing request?
   ↓ yes
update request
   ↓ no
rq = get_request()
   ↓
elv_insert(q, rq, where=ELEVATOR_INSERT_SORT)
   ↓
queue is ready to dispatch
   ↓
q->request_fn()
   ↓
Driver sends request to controller
   ↓
DMA transfer
   ↓
IRQ
   ↓
end_request()
   ↓
bio_endio()

# elevator summary
 Filesystem → BIOs
       |
       v
    generic_make_request
       |
       v
  __make_request()
       |
       v
 +------------------------+
 |   ELEVATOR (I/O Sched) |
 |  - CFQ / Deadline / AS |
 |  - Sorted lists        |
 |  - Merging             |
 |  - Dispatching         |
 +------------------------+
       |
       v
  request_queue->request_fn()
       |
       v
   Block Driver (IDE, SCSI, libata)
       |
       v
   Hardware (DMA, IRQ)


submit_bio()
   ↓
generic_make_request()
   ↓
__make_request()
   ↓
elv_insert()     ← I/O scheduler (elevator)
   ↓
dispatch
   ↓
driver (request_fn)


