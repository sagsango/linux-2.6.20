/*
 * FILE: block/ioctl.c.notes
 *
 * SUBJECT:
 *     Short IDE-style notes for Linux 2.6 generic block-device ioctl handling.
 *
 * PURPOSE:
 *     Explain how generic block-device ioctls are handled, how partition
 *     add/delete/reread works, how generic commands fall back to driver
 *     ioctls, and how blktrace ioctls are routed.
 */

/*
 * ============================================================
 * 0. QUICK SUMMARY
 * ============================================================
 *
 * This file handles ioctl() commands issued on block device files.
 *
 * Example user-space calls:
 *
 *     ioctl(fd, BLKGETSIZE64, &bytes);
 *     ioctl(fd, BLKRRPART);
 *     ioctl(fd, BLKROSET, &flag);
 *     ioctl(fd, BLKTRACESETUP, &setup);
 *
 *
 * The generic block layer handles common commands.
 * If it does not recognize a command, it passes it to the block driver.
 *
 * High-level flow:
 *
 *     user ioctl()
 *          |
 *          v
 *     blkdev_ioctl()
 *          |
 *          +--> generic block ioctl
 *          |
 *          +--> partition ioctl
 *          |
 *          +--> blktrace ioctl
 *          |
 *          +--> driver-specific ioctl fallback
 */

/*
 * ============================================================
 * 1. WHERE THIS FILE FITS
 * ============================================================
 *
 * User space:
 *
 *     fd = open("/dev/sda", O_RDONLY);
 *     ioctl(fd, BLKGETSIZE64, &size);
 *
 * Kernel:
 *
 *     VFS
 *       |
 *       v
 *     block device file operations
 *       |
 *       v
 *     blkdev_ioctl()
 *       |
 *       v
 *     generic block ioctl handling
 *
 *
 * This file is the command dispatcher for block-device ioctl commands.
 */

/*
 * ============================================================
 * 2. IMPORTANT OBJECTS
 * ============================================================
 *
 * struct block_device *bdev
 *
 *     Represents opened block device or partition.
 *
 *     Important fields:
 *
 *         bd_disk
 *             parent gendisk
 *
 *         bd_contains
 *             whole-disk block_device
 *
 *         bd_part
 *             partition metadata
 *
 *         bd_inode
 *             inode with device size
 *
 *         bd_mutex
 *             protects partition/open state
 *
 *
 * struct gendisk *disk
 *
 *     Represents whole disk.
 *
 *     Contains:
 *
 *         minors
 *         part[]
 *         fops
 *         policy
 *
 *
 * disk->fops
 *
 *     Driver block-device operations.
 *
 *     May provide:
 *
 *         unlocked_ioctl
 *         ioctl
 *         compat_ioctl
 *         getgeo
 */

/*
 * ============================================================
 * 3. blkpg_ioctl()
 * ============================================================
 *
 * Purpose:
 *     Handle BLKPG partition manipulation ioctl.
 *
 * Supports:
 *
 *     BLKPG_ADD_PARTITION
 *     BLKPG_DEL_PARTITION
 *
 *
 * Security:
 *
 *     requires CAP_SYS_ADMIN
 *
 *
 * Only allowed on whole disk:
 *
 *     bdev == bdev->bd_contains
 *
 * Not allowed directly on a partition.
 */

/*
 * ------------------------------------------------------------
 * 3.1 BLKPG_ADD_PARTITION
 * ------------------------------------------------------------
 *
 * Flow:
 *
 *     copy blkpg_ioctl_arg from user
 *     copy blkpg_partition from user
 *     validate whole disk
 *     validate partition number
 *     convert byte start/length to sectors
 *     validate sector_t fit
 *     lock whole-disk bd_mutex
 *     check partition slot unused
 *     check no overlap with existing partitions
 *     add_partition()
 *     unlock
 *
 *
 * Overlap check:
 *
 *     new partition [start, start + length)
 *
 * must not overlap any existing:
 *
 *     existing [s->start_sect, s->start_sect + s->nr_sects)
 */

/*
 * ------------------------------------------------------------
 * 3.2 BLKPG_DEL_PARTITION
 * ------------------------------------------------------------
 *
 * Flow:
 *
 *     validate partition exists
 *     get partition block_device using bdget_disk()
 *     lock partition bd_mutex
 *     if partition has openers:
 *         return -EBUSY
 *
 *     fsync_bdev()
 *     invalidate_bdev()
 *
 *     lock whole disk bd_mutex
 *     delete_partition()
 *     unlock
 *
 *     unlock partition
 *     bdput()
 *
 *
 * Why check bd_openers?
 *
 *     Cannot delete a partition while it is open/mounted/in use.
 */

/*
 * ============================================================
 * 4. blkdev_reread_part()
 * ============================================================
 *
 * Purpose:
 *     Handle BLKRRPART.
 *
 * This rereads the partition table.
 *
 * Flow:
 *
 *     reject non-partitionable disk
 *     reject if called on partition instead of whole disk
 *     require CAP_SYS_ADMIN
 *     trylock bdev->bd_mutex
 *     rescan_partitions()
 *     unlock
 *
 *
 * If lock cannot be taken:
 *
 *     return -EBUSY
 */

/*
 * ============================================================
 * 5. put_* HELPERS
 * ============================================================
 *
 * Helpers:
 *
 *     put_ushort()
 *     put_int()
 *     put_long()
 *     put_ulong()
 *     put_u64()
 *
 * Purpose:
 *
 *     Copy scalar kernel value to user pointer.
 *
 * They wrap:
 *
 *     put_user()
 */

/*
 * ============================================================
 * 6. blkdev_locked_ioctl()
 * ============================================================
 *
 * Purpose:
 *     Handle many generic block ioctls while caller holds big kernel lock
 *     in this old kernel.
 *
 * Important commands:
 *
 *     BLKRAGET / BLKFRAGET
 *         get read-ahead in 512-byte sectors
 *
 *     BLKRASET / BLKFRASET
 *         set read-ahead
 *         requires CAP_SYS_ADMIN
 *
 *     BLKROGET
 *         get read-only state
 *
 *     BLKBSZGET
 *         get logical block size
 *
 *     BLKSSZGET
 *         get hardware sector size
 *
 *     BLKSECTGET
 *         get max sectors per request
 *
 *     BLKBSZSET
 *         set logical block size
 *         requires CAP_SYS_ADMIN
 *
 *     BLKPG
 *         add/delete partition
 *
 *     BLKRRPART
 *         reread partition table
 *
 *     BLKGETSIZE
 *         get size in 512-byte sectors as unsigned long
 *
 *     BLKGETSIZE64
 *         get size in bytes as u64
 *
 *     BLKTRACE*
 *         route to blktrace
 */

/*
 * ------------------------------------------------------------
 * 6.1 Read-ahead ioctls
 * ------------------------------------------------------------
 *
 * BLKRAGET / BLKFRAGET:
 *
 *     bdi = blk_get_backing_dev_info(bdev)
 *     return bdi->ra_pages converted to 512-byte sectors
 *
 *
 * BLKRASET / BLKFRASET:
 *
 *     requires CAP_SYS_ADMIN
 *     sets bdi->ra_pages
 */

/*
 * ------------------------------------------------------------
 * 6.2 Size ioctls
 * ------------------------------------------------------------
 *
 * BLKGETSIZE:
 *
 *     returns:
 *
 *         bdev->bd_inode->i_size >> 9
 *
 *     Unit:
 *
 *         512-byte sectors
 *
 *
 * BLKGETSIZE64:
 *
 *     returns:
 *
 *         bdev->bd_inode->i_size
 *
 *     Unit:
 *
 *         bytes
 */

/*
 * ------------------------------------------------------------
 * 6.3 Block-size ioctls
 * ------------------------------------------------------------
 *
 * BLKBSZGET:
 *
 *     logical block size
 *
 * BLKSSZGET:
 *
 *     hardware sector size
 *
 * BLKBSZSET:
 *
 *     set logical block size
 *
 *     requires:
 *
 *         CAP_SYS_ADMIN
 *         bd_claim()
 *
 *     Then:
 *
 *         set_blocksize()
 *         bd_release()
 */

/*
 * ------------------------------------------------------------
 * 6.4 blktrace ioctls
 * ------------------------------------------------------------
 *
 * Commands:
 *
 *     BLKTRACESETUP
 *     BLKTRACESTART
 *     BLKTRACESTOP
 *     BLKTRACETEARDOWN
 *
 * Flow:
 *
 *     blkdev_locked_ioctl()
 *          |
 *          v
 *     blk_trace_ioctl()
 *
 * This connects block-device ioctl control to blktrace.
 */

/*
 * ============================================================
 * 7. blkdev_driver_ioctl()
 * ============================================================
 *
 * Purpose:
 *     Call driver-specific ioctl implementation.
 *
 * Priority:
 *
 *     1. disk->fops->unlocked_ioctl
 *     2. disk->fops->ioctl under lock_kernel()
 *     3. return -ENOTTY
 *
 *
 * This is the fallback for commands not handled generically.
 */

/*
 * ============================================================
 * 8. blkdev_ioctl()
 * ============================================================
 *
 * Purpose:
 *     Top-level block-device ioctl dispatcher.
 *
 * Flow:
 *
 *     blkdev_ioctl(inode, file, cmd, arg)
 *          |
 *          v
 *     get bdev from inode
 *     get disk from bdev
 *          |
 *          v
 *     special early cases:
 *          BLKFLSBUF
 *          BLKROSET
 *          HDIO_GETGEO
 *          |
 *          v
 *     otherwise:
 *          lock_kernel()
 *          blkdev_locked_ioctl()
 *          unlock_kernel()
 *          |
 *          v
 *     if generic handler says unknown:
 *          blkdev_driver_ioctl()
 */

/*
 * ------------------------------------------------------------
 * 8.1 BLKFLSBUF
 * ------------------------------------------------------------
 *
 * Flush buffers.
 *
 * Flow:
 *
 *     first try driver ioctl
 *
 *     if driver does not handle:
 *         require CAP_SYS_ADMIN
 *         fsync_bdev()
 *         invalidate_bdev()
 *
 *
 * Purpose:
 *
 *     write dirty buffers and invalidate cached blocks.
 */

/*
 * ------------------------------------------------------------
 * 8.2 BLKROSET
 * ------------------------------------------------------------
 *
 * Set read-only state.
 *
 * Flow:
 *
 *     first try driver ioctl
 *
 *     if driver does not handle:
 *         require CAP_SYS_ADMIN
 *         get flag from user
 *         set_device_ro()
 */

/*
 * ------------------------------------------------------------
 * 8.3 HDIO_GETGEO
 * ------------------------------------------------------------
 *
 * Get old disk geometry:
 *
 *     cylinders / heads / sectors / start
 *
 * Flow:
 *
 *     require arg pointer
 *     require disk->fops->getgeo
 *     geo.start = get_start_sect(bdev)
 *     driver fills geometry
 *     copy_to_user()
 *
 *
 * Historical note:
 *
 *     This is legacy CHS-style geometry.
 */

/*
 * ============================================================
 * 9. compat_blkdev_ioctl()
 * ============================================================
 *
 * Purpose:
 *     32-bit compatibility ioctl path on 64-bit kernel.
 *
 * Flow:
 *
 *     get bdev/disk
 *     if driver has compat_ioctl:
 *         call it under lock_kernel()
 *
 * Otherwise:
 *
 *     return -ENOIOCTLCMD
 *
 *
 * Comment explains:
 *
 *     most generic ioctls are handled by normal fallback path.
 */

/*
 * ============================================================
 * 10. COMPLETE GENERIC IOCTL FLOW
 * ============================================================
 *
 *     user ioctl(fd, cmd, arg)
 *          |
 *          v
 *     VFS
 *          |
 *          v
 *     blkdev_ioctl()
 *          |
 *          +--> BLKFLSBUF
 *          +--> BLKROSET
 *          +--> HDIO_GETGEO
 *          |
 *          v
 *     blkdev_locked_ioctl()
 *          |
 *          +--> size/read-only/block-size/readahead
 *          +--> BLKPG partition add/delete
 *          +--> BLKRRPART reread partitions
 *          +--> BLKTRACE control
 *          |
 *          v
 *     if unknown:
 *          |
 *          v
 *     blkdev_driver_ioctl()
 *          |
 *          v
 *     driver-specific ioctl
 */

/*
 * ============================================================
 * 11. IMPORTANT SECURITY CHECKS
 * ============================================================
 *
 * Requires CAP_SYS_ADMIN:
 *
 *     BLKPG_ADD_PARTITION
 *     BLKPG_DEL_PARTITION
 *     BLKRRPART
 *     BLKRASET / BLKFRASET
 *     BLKBSZSET
 *     BLKFLSBUF
 *     BLKROSET
 *
 *
 * Reason:
 *
 *     These commands can alter device state, partition table, cache,
 *     read-only policy, or block size.
 */

/*
 * ============================================================
 * 12. MENTAL MODEL
 * ============================================================
 *
 * This file is the generic ioctl router for block devices.
 *
 * It handles common block-layer commands itself:
 *
 *     size
 *     read-only state
 *     block size
 *     read-ahead
 *     partition add/delete
 *     partition reread
 *     blktrace
 *
 * If the command is device-specific:
 *
 *     pass to disk driver.
 *
 *
 * One-line summary:
 *
 *     block/ioctl.c implements the generic block-device ioctl dispatcher,
 *     handling common disk/partition/cache/tracing commands and forwarding
 *     unknown commands to the underlying block driver.
 */

