# EXT Filesystem & Disk Driver Integration - Comprehensive Technical Notes

## TABLE OF CONTENTS
1. [BACKGROUND](#background)
2. [SYSTEM OVERVIEW](#system-overview)
3. [DISK DRIVER LAYER](#disk-driver-layer)
4. [BLOCK I/O SUBSYSTEM](#block-io-subsystem)
5. [BUFFER HEAD & CACHE](#buffer-head--cache)
6. [BIO LAYER](#bio-layer)
7. [VFS INTEGRATION](#vfs-integration)
8. [EXT FILESYSTEM MOUNT](#ext-filesystem-mount)
9. [SUPERBLOCK INITIALIZATION](#superblock-initialization)
10. [INODE & BLOCK MANAGEMENT](#inode--block-management)
11. [DATA FLOW & I/O PATH](#data-flow--io-path)
12. [COMPLETE EXECUTION TRACE](#complete-execution-trace)

---

## BACKGROUND

### Storage Architecture Evolution

```
Timeline of Linux storage I/O:
  
  Early 2.4 (1999-2001):
    ├─ Buffer cache (per-block-device)
    ├─ Page cache (per-page)
    └─ Separate caches causing issues
  
  2.5+ (2001-2003):
    ├─ Unified page-based buffer cache
    ├─ BIO abstraction introduced
    ├─ I/O scheduler framework
    └─ Dynamic request queue management
  
  2.6.20 (2007):
    ├─ Mature BIO layer
    ├─ Multiple I/O schedulers (noop, deadline, CFQ, AS)
    ├─ Intelligent request merging
    └─ Full integration with filesystems
```

### Design Philosophy

The Linux storage subsystem is built on several key principles:

1. **Separation of Concerns**
   - Filesystem (ext3) doesn't know about disk hardware
   - Disk driver doesn't know about filesystem structure
   - Block I/O layer bridges the gap

2. **Abstraction Layers**
   - VFS: Generic filesystem interface
   - Block layer: Abstract device operations
   - Drivers: Hardware-specific implementations

3. **Performance Optimization**
   - Request merging: Combine sequential I/O
   - I/O scheduling: Reorder requests for disk efficiency
   - Read-ahead: Predict future block reads
   - Write-back: Delay writes for batching

---

## SYSTEM OVERVIEW

### Complete Storage Stack

```
┌────────────────────────────────────────────────────────┐
│           Application / System Call Layer              │
│  (mount(), open(), read(), write(), sync(), etc.)      │
└────────────────────┬─────────────────────────────────┘
                     │ ssize_t = sys_read(fd, buf, count)
                     │ int = sys_write(fd, buf, count)
                     ↓
┌────────────────────────────────────────────────────────┐
│    Virtual Filesystem Switch (VFS) Layer               │
│  (fs/namei.c, fs/read_write.c, fs/open.c)             │
│  - Generic inode/dentry operations                     │
│  - File descriptor management                          │
│  - Generic read/write paths                            │
└────────────────────┬─────────────────────────────────┘
                     │ struct address_space operations
                     │ struct file_operations
                     ↓
┌────────────────────────────────────────────────────────┐
│     EXT3 Filesystem Layer (fs/ext3/)                   │
│  - Superblock, inode, directory operations             │
│  - ext3_file_operations, ext3_dir_operations           │
│  - Journal management (fs/jbd/)                        │
│  - Block allocation/deallocation                       │
└────────────────────┬─────────────────────────────────┘
                     │ struct buffer_head (fs/buffer.c)
                     │ get_block_t callbacks
                     ↓
┌────────────────────────────────────────────────────────┐
│      Block I/O Buffer Cache Layer                      │
│  (fs/buffer.c, fs/bio.c)                              │
│  - Buffer heads (per 512B or 4KB block)                │
│  - Page cache integration                              │
│  - BIO abstraction (bio structures)                    │
│  - Request queue management                            │
└────────────────────┬─────────────────────────────────┘
                     │ struct bio (bio vectors)
                     │ struct request_queue
                     ↓
┌────────────────────────────────────────────────────────┐
│     Generic Block Layer (block/ll_rw_blk.c)           │
│  - Request creation & merging                          │
│  - I/O scheduler (elevator)                            │
│  - Queue management                                    │
│  - Request dispatch to device drivers                  │
└────────────────────┬─────────────────────────────────┘
                     │ struct request
                     │ make_request_fn, request_fn
                     ↓
┌────────────────────────────────────────────────────────┐
│     Disk Device Drivers (drivers/block/, drivers/ata/)|
│  - ATA, SCSI, USB, NVME drivers                       │
│  - Hardware-specific command construction              │
│  - DMA setup and management                            │
│  - Interrupt handling                                  │
└────────────────────┬─────────────────────────────────┘
                     │ IRQ: Interrupt
                     ↓
┌────────────────────────────────────────────────────────┐
│     Storage Hardware                                   │
│  - IDE/SATA disk, SAS, SCSI, USB drive, Flash SSD     │
│  - Mechanical or electronic storage                    │
└────────────────────────────────────────────────────────┘
```

### Request Flow (Write Path Example)

```
User Space Application:
  write(fd, buf, 1024)
       ↓
   [sys_write() syscall]
       ↓
VFS Layer:
  generic_file_write_iter()
       ├─ Address space mapping
       ├─ Page fault handling
       └─ Dirty page tracking
       ↓
EXT3 Filesystem:
  ext3_write_begin()
       ├─ ext3_get_block() [maps logical block to physical]
       ├─ Buffer head creation
       └─ Journal transaction start
       ↓
Buffer Cache:
  __getblk() [get or create buffer_head]
       ├─ Buffer cache lookup (hash table)
       ├─ If miss: allocate new buffer_head
       └─ Link to page
       ↓
Journal (if configured):
  journal_start(), mark_buffer_dirty()
       └─ Defer physical write until commit
       ↓
Write-Back System:
  balance_dirty_pages()
       ├─ Periodic flush daemon
       ├─ Dirty page limit enforcement
       └─ Triggered by sync(), fsync()
       ↓
Block I/O Layer:
  submit_bh() [submit buffer head]
       ├─ Create BIO structure
       ├─ Create request
       └─ Insert into I/O scheduler queue
       ↓
I/O Scheduler (e.g., CFQ):
  elevator_add_req_fn()
       ├─ Merge with adjacent requests if possible
       ├─ Reorder for disk efficiency
       └─ Maintain read/write fairness
       ↓
Device Driver:
  request_fn() [called by blk_run_queue()]
       ├─ Dequeue request
       ├─ Build disk command (ATA/SCSI)
       ├─ Setup DMA buffers
       └─ Issue command to hardware
       ↓
Storage Hardware:
  Write data from memory to disk
       ↓
Completion Interrupt:
  IRQ Handler → Device Driver → Block Layer
       ├─ end_io callback
       ├─ Mark buffer_head complete
       └─ Wake up waiting processes
       ↓
User Space:
  write() returns with bytes written
```

---

## DISK DRIVER LAYER

### Driver Architecture

```
Disk Device Driver Components:

┌─────────────────────────────────────────┐
│     Disk Driver (e.g., ATA/SATA)        │
├─────────────────────────────────────────┤
│ 1. Module Initialization (module_init)  │
│    ├─ Driver registration               │
│    └─ Hardware detection                │
│                                         │
│ 2. Device Discovery & Setup             │
│    ├─ PCI/Probe functions               │
│    ├─ Device reset/initialization       │
│    └─ Queue setup                       │
│                                         │
│ 3. Request Processing (request_fn)      │
│    ├─ Dequeue request from queue        │
│    ├─ Build CDB/command                 │
│    ├─ DMA buffer setup                  │
│    └─ Issue to hardware                 │
│                                         │
│ 4. Interrupt Handling (IRQ handler)     │
│    ├─ Status/error detection            │
│    ├─ Data transfer completion          │
│    └─ End-IO callback                   │
│                                         │
│ 5. Error Recovery                       │
│    ├─ Timeout handling                  │
│    ├─ Command retry                     │
│    └─ Link/device recovery              │
└─────────────────────────────────────────┘
```

### Request Queue Structure

```c
struct request_queue {
    /* Queue operations */
    request_fn_proc *request_fn;           /* Queue handler */
    make_request_fn *make_request_fn;      /* BIO submission */
    
    /* Queue state */
    struct list_head queue_head;           /* Pending requests */
    struct request *rq;                    /* Current request array */
    
    /* Configuration */
    unsigned int nr_requests;              /* Max requests in queue */
    unsigned int nr_congestion_on;         /* Start blocking */
    unsigned int nr_congestion_off;        /* Resume accepting */
    
    /* I/O Scheduling */
    struct elevator_queue *elevator;       /* I/O scheduler (CFQ, deadline, etc.) */
    
    /* Plugging (batch requests) */
    struct blk_plug_cb *plug;             /* Defer request dispatch */
    
    /* Statistics */
    unsigned long sectors[2];              /* Read/write sectors */
    unsigned long ios[2];                  /* Read/write count */
    
    /* Device properties */
    sector_t hardsect_size;               /* Hardware sector size */
    sector_t max_hw_sectors;              /* Max sectors per request */
    unsigned int max_phys_segments;       /* Max segment count */
};
```

### Device Initialization Example (Pseudo-SATA)

```c
/* Module init: Called when driver loads */
static int __init ata_init(void)
{
    /* 1. Register driver with kernel */
    int ret = pci_register_driver(&ata_pci_driver);
    
    /* 2. Register interrupt handler globally */
    printk("ATA: Init complete\n");
    
    return ret;
}

static int ata_pci_probe(struct pci_dev *pdev,
                         const struct pci_device_id *id)
{
    struct ata_host *host;
    struct ata_port *ap;
    
    /* 3. Allocate host structure */
    host = ata_host_alloc_pinfo(&pdev->dev, ppi, nr_ports);
    
    /* 4. Enable PCI device */
    pci_enable_device(pdev);
    pci_set_master(pdev);
    
    /* 5. Map PCI memory/IO regions */
    iobase = pci_resource_start(pdev, 5);
    
    /* 6. Set up each port */
    for (i = 0; i < host->n_ports; i++) {
        ap = host->ports[i];
        
        /* Port address setup */
        ap->ioaddr.cmd_addr = iobase + 0x80 * i;
        ap->ioaddr.ctl_addr = iobase + 0x80 * i + 0x14;
        
        /* DMA setup */
        ap->ioaddr.bmdma_addr = iobase + 0x0C + (0x08 * i);
        
        /* Set up operations */
        ap->ops = &ata_sff_port_ops;
    }
    
    /* 7. Request queue creation */
    for (i = 0; i < ATA_MAX_QUEUE; i++) {
        struct request_queue *q = blk_init_queue(ata_request_fn,
                                                 &host->lock);
        ap->queue = q;
        
        /* Configure queue */
        blk_queue_max_sectors(q, ATA_MAX_SECTORS);
        blk_queue_max_phys_segments(q, ATA_MAX_SEGMENTS);
    }
    
    /* 8. Register block device */
    device_add_disk(host->dev, ap->disk);
    
    /* 9. Start port (reset and probe) */
    ata_host_activate(host, pdev->irq, ata_intr, 0, &ata_sht);
    
    return 0;
}

/* Request processing: Called when queue has requests */
static void ata_request_fn(struct request_queue *q)
{
    struct ata_port *ap = q->queuedata;
    struct request *rq;
    
    while (!list_empty(&q->queue_head)) {
        /* 10. Dequeue request from I/O scheduler */
        rq = elv_next_request(q);
        if (!rq)
            break;
        
        if (rq->cmd_type == REQ_TYPE_FS) {
            /* 11. Filesystem request: build ATA command */
            struct ata_taskfile tf;
            
            /* Extract command info */
            sector_t sector = rq->sector;
            int sectors = rq->nr_sectors;
            int write = rq->cmd_flags & REQ_WRITE;
            
            /* Build command */
            tf.command = write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA;
            tf.lbal = (sector) & 0xFF;
            tf.lbam = (sector >> 8) & 0xFF;
            tf.lbah = (sector >> 16) & 0xFF;
            tf.device = ((sector >> 24) & 0x0F) | 0x40;
            tf.nsect = sectors & 0xFF;
            
            /* 12. Setup DMA buffers */
            ata_sg_init(rq, bio_data_dir(rq->bio));
            
            /* 13. Issue command to device */
            ata_qc_prep(ap->qc);
            ap->ops->exec_command(ap, &tf);
        }
        
        /* Mark request as in-flight */
        blkdev_dequeue_request(rq);
    }
}

/* Interrupt handler: Called on device completion */
static irqreturn_t ata_intr(int irq, void *dev_instance)
{
    struct ata_host *host = dev_instance;
    int handled = 0;
    
    /* 14. Check all ports for completion */
    for (i = 0; i < host->n_ports; i++) {
        struct ata_port *ap = host->ports[i];
        u8 status = ioread8(ap->ioaddr.status_addr);
        
        if (status & ATA_BUSY)
            continue;  /* Not done yet */
        
        if (status & (ATA_ERR | ATA_DF)) {
            /* 15. Error occurred */
            ap->qc->err_mask |= AC_ERR_DEV;
        } else {
            /* 16. Success: Mark buffer complete */
            ap->qc->err_mask = 0;
        }
        
        /* 17. Call completion handler */
        ata_qc_complete(ap->qc);
        handled = 1;
    }
    
    return handled ? IRQ_HANDLED : IRQ_NONE;
}

static void ata_qc_complete(struct ata_queued_cmd *qc)
{
    struct request *rq = qc->private_data;
    struct request_queue *q = rq->q;
    
    /* 18. Update request status */
    if (qc->err_mask) {
        rq->errors = -EIO;
    } else {
        rq->errors = 0;
    }
    
    /* 19. Call end_io callback (bio-level) */
    blk_complete_request(rq);
}

static void end_request_last(struct request *rq)
{
    /* 20. Notify buffer/bio layer of completion */
    struct bio *bio = rq->bio;
    
    while (bio) {
        struct bio *next = bio->bi_next;
        bio->bi_private->end_io(bio, rq->errors);
        bio = next;
    }
    
    /* 21. Dequeue next request */
    __blk_run_queue(rq->q);
}
```

---

## BLOCK I/O SUBSYSTEM

### Block Device Registration

```
System Boot:
  ↓
Kernel detects storage device (e.g., PCI)
  ↓
Driver module loads or compiled-in
  ↓
driver_register(&ata_driver)
  ├─ device_create(&dev_class, "sdX")
  └─ register_blkdev(major, "sd")
  
Kernel creates:
  /dev/sdX (block device file)
  /sys/block/sdX/queue/ (sysfs)
  
Block device structure:
  struct gendisk {
      int major;           /* Major device number (e.g., 8 for SATA) */
      int first_minor;     /* Starting minor number */
      int minors;          /* Number of partitions */
      char disk_name[32];  /* "sda", "sdb", etc. */
      struct request_queue *queue;  /* I/O queue */
      struct block_device_operations *fops;
      struct disk_part_tbl *part_tbl;  /* Partitions */
      int nr_parts;        /* Number of partitions */
  };
```

### Request Queue Operations

```c
/* Create a request queue */
struct request_queue *
blk_init_queue(request_fn_proc *rfn, spinlock_t *lock)
{
    struct request_queue *q;
    
    /* Allocate queue structure */
    q = kmem_cache_alloc(requestq_cachep, GFP_KERNEL);
    
    /* Initialize request list */
    INIT_LIST_HEAD(&q->queue_head);
    
    /* Allocate request pool (typically 64-256 requests) */
    for (i = 0; i < nr_requests; i++) {
        rq = kmem_cache_alloc(request_cachep, GFP_KERNEL);
        list_add(&rq->queuelist, &q->rq_pool);
    }
    
    /* Set request handler */
    q->request_fn = rfn;
    q->make_request_fn = __make_request;  /* Default: merge requests */
    
    /* Initialize I/O scheduler (e.g., CFQ) */
    q->elevator = elevator_alloc(q, &elevator_cfq);
    q->elevator->elevator_init_fn(q);
    
    /* Set queue properties */
    blk_queue_max_sectors(q, 256);        /* Max sectors per request */
    blk_queue_max_phys_segments(q, 128);  /* Max scatter-gather segs */
    blk_queue_hardsect_size(q, 512);      /* Hardware sector size */
    
    /* Congestion thresholds */
    blk_queue_congestion_threshold(q);    /* Calculate on/off thresholds */
    
    return q;
}

/* Submit a BIO (bio vector) to queue */
int generic_make_request(struct bio *bio)
{
    struct request_queue *q = bdev_get_queue(bio->bi_bdev);
    
    /* Chain multiple BIOs */
    do {
        struct bio *next = bio->bi_next;
        bio->bi_next = NULL;
        
        /* Call queue's make_request function */
        if (q->make_request_fn)
            q->make_request_fn(q, bio);
        else
            __make_request(q, bio);
        
        bio = next;
    } while (bio);
    
    return 0;
}

/* Default make_request: Merge requests intelligently */
static int __make_request(request_queue_t *q, struct bio *bio)
{
    struct request *req;
    int el_ret;
    
    /* 1. Try to find adjacent request to merge with */
    el_ret = elv_merge(q, &req, bio);
    
    switch (el_ret) {
    case ELEVATOR_BACK_MERGE:
        /* Append bio to end of existing request */
        if (__rq_for_each_bio(req->bio, req))
            req->biotail->bi_next = bio;
        else
            req->bio = bio;
        req->biotail = bio;
        req->nr_sectors += bio_sectors(bio);
        req->ioprio = ioprio_best(req->ioprio, bio_prio(bio));
        break;
        
    case ELEVATOR_FRONT_MERGE:
        /* Prepend bio to front of existing request */
        bio->bi_next = req->bio;
        req->bio = bio;
        req->nr_sectors += bio_sectors(bio);
        break;
        
    case ELEVATOR_NO_MERGE:
        /* No merge possible: create new request */
        req = get_request(q, bio_data_dir(bio), bio);
        if (!req)
            goto out_queue;
        
        req->bio = req->biotail = bio;
        req->nr_sectors = bio_sectors(bio);
        req->sector = bio->bi_sector;
        
        /* Let elevator decide where to insert */
        el_ret = elv_insert(q, req, ELEVATOR_INSERT_SORT);
        
        /* If elevator didn't insert, queue manually */
        if (el_ret != ELEVATOR_INSERT_SORT)
            generic_request_insert(q, req);
        break;
    }
    
    /* 2. Run queue if not already running */
    if (!blk_queue_plugged(q))
        q->request_fn(q);
    
    return 0;
    
out_queue:
    bio_endio(bio, bio->bi_size, -ENOMEM);
    return 0;
}

/* I/O Scheduler: Decide request ordering */
struct elevator_queue *
elevator_alloc(struct request_queue *q, const struct elevator_type *e)
{
    struct elevator_queue *eq;
    
    eq = kmem_cache_alloc(elv_cache, GFP_KERNEL);
    eq->elevator_type = e;
    
    /* Call elevator-specific init */
    eq->elevator_init_fn = e->elevator_init_fn;
    
    return eq;
}

/* CFQ (Completely Fair Queueing) Scheduler Example */
static int cfq_init_queue(struct request_queue *q)
{
    struct cfq_data *cfqd;
    
    cfqd = kmem_cache_alloc(cfq_pool, GFP_KERNEL);
    
    /* Create per-process queues */
    cfqd->cfq_hash = kmalloc(CFQ_HASH_SIZE * sizeof(struct hlist_head),
                             GFP_KERNEL);
    for (i = 0; i < CFQ_HASH_SIZE; i++)
        INIT_HLIST_HEAD(&cfqd->cfq_hash[i]);
    
    /* Priority queues: async write, sync write, sync read */
    INIT_LIST_HEAD(&cfqd->rr_list[CFQ_AIOPRIO_LISTS]);
    
    q->elevator->elevator_data = cfqd;
    q->elevator->elevator_type->elevator_dispatch_fn = cfq_dispatch_requests;
    
    return 0;
}
```

---

## BUFFER HEAD & CACHE

### Buffer Head Structure

```c
struct buffer_head {
    unsigned long b_state;           /* Buffer state flags */
    struct buffer_head *b_this_page; /* Buffers within page */
    struct page *b_page;             /* Containing page */
    
    sector_t b_blocknr;              /* Logical block number */
    size_t b_size;                   /* Buffer size (512B-4KB) */
    char *b_data;                    /* Virtual address */
    
    struct block_device *b_bdev;     /* Block device */
    
    bh_end_io_t *b_end_io;          /* Completion callback */
    void *b_private;                 /* Completion data */
    
    struct list_head b_assoc_buffers; /* Hash list link */
};

/* Buffer state flags (b_state) */
BH_Uptodate      /* Buffer has valid data from disk */
BH_Dirty         /* Buffer contains unsaved data */
BH_Lock          /* Buffer I/O in progress */
BH_Req           /* Buffer has pending request */
BH_Mapped        /* Buffer has been mapped (get_block called) */
BH_New           /* Newly allocated block */
BH_Async_Read    /* Async read in progress */
BH_Async_Write   /* Async write in progress */
BH_Delay         /* Write should be delayed (journal) */
BH_Boundary      /* Buffer at boundary (reorder required) */
BH_Write_EIO     /* Write failed with I/O error */
BH_Ordered       /* Write must complete before next request */
```

### Buffer Cache Management

```
Buffer Cache Hash Table:

┌─────────────────────────────────────┐
│      Hash(bdev, blocknr) mod N      │
├─────────────────────────────────────┤
│  bucket[0] → [bh1] → [bh2] → ...   │
│  bucket[1] → [bh3] → ...           │
│  ...                                 │
│  bucket[N-1] → [bh_last] → ...     │
└─────────────────────────────────────┘

Operations:
  __find_get_block(): Look up buffer in cache
    ├─ Hash: hash_fn(bdev, block)
    ├─ Search chain
    └─ Return if found, NULL otherwise
  
  __getblk(): Get or create buffer
    ├─ Try __find_get_block() first
    ├─ If miss: __getblk_slow()
    │   ├─ Allocate buffer_head
    │   ├─ Allocate page (if needed)
    │   ├─ Insert into hash table
    │   └─ Return
    └─ Return
```

### Buffer Operations

```c
/* Mark buffer as dirty (needs write) */
void mark_buffer_dirty(struct buffer_head *bh)
{
    if (!buffer_dirty(bh)) {
        set_buffer_dirty(bh);
        
        /* Link to dirty list for write-back */
        write_lock(&mapping->private_lock);
        list_add(&bh->b_assoc_buffers, &mapping->private_list);
        write_unlock(&mapping->private_lock);
        
        /* Mark page as dirty */
        __set_page_dirty_nobuffers(bh->b_page);
    }
}

/* Get a buffer head for a specific block */
struct buffer_head *
__getblk(struct block_device *bdev, sector_t block, int size)
{
    struct buffer_head *bh = __find_get_block(bdev, block, size);
    
    if (bh == NULL)
        bh = __getblk_slow(bdev, block, size);
    
    return bh;
}

static struct buffer_head *
__getblk_slow(struct block_device *bdev, sector_t block, int size)
{
    /* Allocate buffer_head */
    struct buffer_head *bh;
    bh = kmem_cache_alloc(bh_cachep, GFP_NOFS);
    
    /* Allocate or get page */
    struct page *page = grab_cache_page(bdev->bd_inode->i_mapping,
                                        block / (PAGE_SIZE / size));
    
    /* Initialize buffer */
    bh->b_bdev = bdev;
    bh->b_blocknr = block;
    bh->b_size = size;
    bh->b_data = page_address(page) + offset_in_page;
    bh->b_page = page;
    
    /* Insert into cache hash table */
    spin_lock(&hash_lock);
    hash_insert_bh(bh);
    spin_unlock(&hash_lock);
    
    return bh;
}

/* Submit buffer for I/O */
void submit_bh(int rw, struct buffer_head *bh)
{
    struct bio *bio;
    
    /* Mark buffer as locked */
    lock_buffer(bh);
    
    /* Create BIO for this buffer */
    bio = bio_alloc(GFP_NOIO, 1);
    bio->bi_sector = bh->b_blocknr;
    bio->bi_bdev = bh->b_bdev;
    bio->bi_io_vec[0].bv_page = bh->b_page;
    bio->bi_io_vec[0].bv_len = bh->b_size;
    bio->bi_io_vec[0].bv_offset = bh_offset(bh);
    bio->bi_vcnt = 1;
    bio->bi_idx = 0;
    bio->bi_size = bh->b_size;
    bio->bi_end_io = end_buffer_async_write;  /* or read */
    bio->bi_private = bh;
    
    /* Submit BIO to block device */
    submit_bio(rw, bio);
}

/* Completion callback: Called when I/O finishes */
void end_buffer_async_read(struct bio *bio, int uptodate)
{
    struct buffer_head *bh = bio->bi_private;
    
    if (uptodate) {
        set_buffer_uptodate(bh);
    } else {
        clear_buffer_uptodate(bh);
        buffer_io_error(bh);
    }
    
    unlock_buffer(bh);
    put_bh(bh);
}

void end_buffer_async_write(struct bio *bio, int uptodate)
{
    struct buffer_head *bh = bio->bi_private;
    
    if (uptodate) {
        set_buffer_uptodate(bh);
    } else {
        clear_buffer_uptodate(bh);
        set_buffer_write_io_error(bh);
        buffer_io_error(bh);
    }
    
    unlock_buffer(bh);
    put_bh(bh);
}

/* Synchronously wait for buffer */
void wait_on_buffer(struct buffer_head *bh)
{
    might_sleep();
    if (buffer_locked(bh))
        __wait_on_buffer(bh);
}
```

---

## BIO LAYER

### BIO Structure

```c
struct bio {
    sector_t bi_sector;              /* Starting sector */
    struct bio *bi_next;             /* Next bio in chain */
    struct block_device *bi_bdev;    /* Target device */
    
    unsigned int bi_vcnt;            /* Number of vectors */
    unsigned int bi_idx;             /* Current vector index */
    unsigned int bi_size;            /* Total bytes in bio */
    
    struct bio_vec *bi_io_vec;       /* Scatter-gather vector */
    struct bio_integrity_payload *bi_integrity;
    
    bio_end_io_t *bi_end_io;        /* Completion callback */
    void *bi_private;                /* Context (buffer_head, etc.) */
    
    unsigned short bi_flags;         /* Flags (READ/WRITE, etc.) */
    unsigned short bi_ioprio;        /* I/O priority */
    
    atomic_t bi_cnt;                 /* Reference count */
};

struct bio_vec {
    struct page *bv_page;            /* Memory page */
    unsigned int bv_len;             /* Length in this page */
    unsigned int bv_offset;          /* Offset within page */
};
```

### BIO Operations

```c
/* Allocate a BIO */
struct bio *
bio_alloc(gfp_t gfp_mask, int nr_iovecs)
{
    struct bio *bio;
    
    /* Allocate bio structure */
    bio = mempool_alloc(bio_pool, gfp_mask);
    
    /* Allocate vectors if needed */
    if (nr_iovecs > 0) {
        bio->bi_io_vec = kmalloc(nr_iovecs * sizeof(struct bio_vec),
                                gfp_mask);
    }
    
    bio->bi_vcnt = 0;
    bio->bi_idx = 0;
    bio->bi_size = 0;
    atomic_set(&bio->bi_cnt, 1);
    
    return bio;
}

/* Submit BIO to queue */
void submit_bio(int rw, struct bio *bio)
{
    struct request_queue *q = bdev_get_queue(bio->bi_bdev);
    
    if (unlikely(!q)) {
        bio_io_error(bio);
        return;
    }
    
    /* Generic make_request merges/creates requests */
    if (q->make_request_fn != generic_make_request)
        q->make_request_fn(q, bio);
    else
        generic_make_request(bio);
}

/* Get bio completion count (for compound operations) */
void bio_get(struct bio *bio)
{
    atomic_inc(&bio->bi_cnt);
}

/* Decrement bio refcount and complete if zero */
void bio_endio(struct bio *bio, unsigned int bytes_done, int error)
{
    bio->bi_size -= bytes_done;
    
    if (bio->bi_size > 0)
        return;  /* Partial completion */
    
    if (bio->bi_end_io)
        bio->bi_end_io(bio, error);
}

/* Link multiple BIOs together */
void bio_chain(struct bio *bio1, struct bio *bio2)
{
    bio1->bi_next = bio2;
    bio_get(bio2);
}
```

---

## VFS INTEGRATION

### VFS Inode Operations

```c
struct inode_operations {
    /* File operations */
    int (*create)(struct inode *, struct dentry *,
                  int, struct nameidata *);
    struct dentry *(*lookup)(struct inode *, struct dentry *,
                             struct nameidata *);
    int (*link)(struct dentry *, struct inode *, struct dentry *);
    int (*unlink)(struct inode *, struct dentry *);
    int (*symlink)(struct inode *, struct dentry *, const char *);
    int (*mkdir)(struct inode *, struct dentry *, int);
    int (*rmdir)(struct inode *, struct dentry *);
    int (*mknod)(struct inode *, struct dentry *, int, dev_t);
    int (*rename)(struct inode *, struct dentry *,
                  struct inode *, struct dentry *);
    
    /* Attribute operations */
    int (*readlink)(struct dentry *, char __user *, int);
    int (*follow_link)(struct dentry *, struct nameidata *);
    int (*put_link)(struct dentry *, struct nameidata *);
    
    void (*truncate)(struct inode *);
    int (*permission)(struct inode *, int);
    int (*getattr)(struct vfsmount *, struct dentry *, struct kstat *);
    int (*setattr)(struct dentry *, struct iattr *);
};

struct file_operations {
    struct module *owner;
    loff_t (*llseek)(struct file *, loff_t, int);
    ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
    ssize_t (*write)(struct file *, const char __user *,
                     size_t, loff_t *);
    int (*readdir)(struct file *, void *, filldir_t);
    unsigned int (*poll)(struct file *, struct poll_table_struct *);
    int (*ioctl)(struct inode *, struct file *, unsigned int, unsigned long);
    int (*mmap)(struct file *, struct vm_area_struct *);
    int (*open)(struct inode *, struct file *);
    int (*flush)(struct file *, fl_owner_t);
    int (*release)(struct inode *, struct file *);
    int (*fsync)(struct file *, struct dentry *, int);
    int (*aio_read)(struct kiocb *, const struct iovec *,
                    unsigned long, loff_t);
    int (*aio_write)(struct kiocb *, const struct iovec *,
                     unsigned long, loff_t);
};

struct address_space_operations {
    int (*writepage)(struct page *, struct writeback_control *);
    int (*readpage)(struct file *, struct page *);
    int (*write_begin)(struct file *, struct address_space *,
                      loff_t, unsigned, unsigned,
                      struct page **, void **);
    int (*write_end)(struct file *, struct address_space *,
                    loff_t, unsigned, unsigned,
                    struct page *, void *);
    sector_t (*bmap)(struct address_space *, sector_t);
    void (*invalidatepage)(struct page *, unsigned long);
    int (*releasepage)(struct page *, gfp_t);
    int (*direct_IO)(int, struct kiocb *, const struct iovec *,
                     loff_t, unsigned long);
    int (*launder_page)(struct page *);
    int (*is_partially_uptodate)(struct page *, read_descriptor_t *,
                                 unsigned long);
    int (*error_remove_page)(struct address_space *, struct page *);
};
```

---

## EXT FILESYSTEM MOUNT

### Mount Sequence

```
1. User Command:
   mount -t ext3 /dev/sda1 /mnt
   
2. VFS Mount Entry Point:
   do_mount()
   └─ fs_type->get_sb("ext3", flags, dev_name, data)
   └─ ext3_get_sb()
       └─ get_sb_bdev(fs_type, flags, dev_name, data, 
                      ext3_fill_super, mnt)
   
3. Block Device Resolution:
   lookup_bdev("/dev/sda1")
   └─ Find block_device structure
   └─ Call blkdev_get() to open device
   
4. Superblock Creation:
   sget(fs_type, cmp_fn, set_fn, data)
   └─ Allocate super_block structure
   └─ Initialize with ext3_fill_super()
   
5. Filesystem Registration:
   register_filesystem(&ext3_fs_type)
   └─ Add to kernel's supported filesystems
   └─ Enable "mount -t ext3" command

6. VFS Mount:
   vfs_mount_subtree(root_dentry)
   └─ Create mount_point linking mnt_ns to filesystem
```

### ext3_get_sb Flow

```c
static int ext3_get_sb(struct file_system_type *fs_type,
                      int flags, const char *dev_name,
                      void *data, struct vfsmount *mnt)
{
    return get_sb_bdev(fs_type, flags, dev_name, data,
                       ext3_fill_super, mnt);
}

int get_sb_bdev(struct file_system_type *fs_type,
               int flags, const char *dev_name, void *data,
               int (*fill_super)(struct super_block *, void *, int),
               struct vfsmount *mnt)
{
    struct block_device *bdev = NULL;
    struct super_block *sb = NULL;
    struct vfsmount *mnt = NULL;
    int error = 0;
    
    /* 1. Convert device name to block_device */
    bdev = lookup_bdev(dev_name);
    if (IS_ERR(bdev))
        return PTR_ERR(bdev);
    
    /* 2. Open the block device */
    error = blkdev_get(bdev, FMODE_READ | FMODE_WRITE, 0);
    if (error)
        goto out;
    
    /* 3. Get or create superblock */
    sb = sget(fs_type, test_bdev_super, set_bdev_super,
              bdev);
    if (IS_ERR(sb))
        goto out;
    
    /* 4. If fresh superblock, initialize it */
    if (sb->s_root == NULL) {
        sb->s_flags = flags;
        error = fill_super(sb, data, flags & MS_SILENT ? 1 : 0);
        if (error)
            goto out_fail;
        sb->s_flags |= MS_ACTIVE;
    }
    
    /* 5. Create VFS mount */
    mnt = vfs_kern_mount(fs_type, flags, dev_name, data);
    mnt->mnt_sb = sb;
    mnt->mnt_root = dget(sb->s_root);
    
    return simple_set_mnt(mnt, sb);
    
out_fail:
    up_write(&sb->s_umount);
    deactivate_super(sb);
out:
    blkdev_put(bdev);
    return error;
}
```

---

## SUPERBLOCK INITIALIZATION

### ext3_fill_super Deep Dive

```c
static int ext3_fill_super(struct super_block *sb, void *data, int silent)
{
    struct ext3_sb_info *sbi;
    struct ext3_super_block *es;
    struct buffer_head *bh;
    ext3_fsblk_t sb_block;
    int blocksize;
    int hblock;
    
    /* Phase 1: Basic Setup */
    ─────────────────────
    
    /* Allocate kernel-private filesystem info */
    sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
    sb->s_fs_info = sbi;
    
    /* Parse mount options from mount string */
    parse_options(data, sb, &journal_inum, &journal_devnum, NULL, 0);
    
    /* Phase 2: Read Physical Superblock from Disk */
    ─────────────────────────────────────────────────
    
    /* Get blocksize (1KB, 2KB, or 4KB) */
    blocksize = sb_min_blocksize(sb, EXT3_MIN_BLOCK_SIZE);
    
    /* Calculate superblock location */
    sb_block = get_sb_block(&data);  /* Usually block 1 */
    
    /* Read superblock block into buffer */
    bh = sb_bread(sb, sb_block);
    if (!bh) {
        printk("EXT3: unable to read superblock\n");
        goto failed;
    }
    
    /* Point to superblock within buffer */
    es = (struct ext3_super_block *)bh->b_data;
    sbi->s_es = es;
    sbi->s_sbh = bh;  /* Save for later writes */
    
    /* Verify magic number */
    if (es->s_magic != EXT3_SUPER_MAGIC)
        goto cantfind_ext3;
    
    /* Phase 3: Validate and Configure Block Size */
    ──────────────────────────────────────────────
    
    /* Get actual blocksize from superblock */
    blocksize = BLOCK_SIZE << le32_to_cpu(es->s_log_block_size);
    
    if (blocksize != sb->s_blocksize) {
        /* Need to re-read superblock with correct blocksize */
        
        /* Set blocksize in VFS */
        sb_set_blocksize(sb, blocksize);
        
        /* Recalculate logical block number */
        logic_sb_block = (sb_block * EXT3_MIN_BLOCK_SIZE) / blocksize;
        
        /* Re-read superblock buffer */
        brelse(bh);
        bh = sb_bread(sb, logic_sb_block);
        if (!bh) {
            printk("EXT3: Can't read superblock on 2nd try\n");
            goto failed;
        }
        es = (struct ext3_super_block *)bh->b_data;
    }
    
    /* Phase 4: Parse Superblock Metadata */
    ────────────────────────────────────────
    
    /* Get filesystem dimensions */
    sbi->s_blocks_per_group = le32_to_cpu(es->s_blocks_per_group);
    sbi->s_frags_per_group = le32_to_cpu(es->s_frags_per_group);
    sbi->s_inodes_per_group = le32_to_cpu(es->s_inodes_per_group);
    sbi->s_inodes_per_block = blocksize / EXT3_INODE_SIZE(sb);
    sbi->s_itb_per_group = sbi->s_inodes_per_group / 
                           sbi->s_inodes_per_block;
    
    /* Calculate number of block groups */
    sbi->s_groups_count = 
        ((le32_to_cpu(es->s_blocks_count) - 
          le32_to_cpu(es->s_first_data_block) - 1) / 
         EXT3_BLOCKS_PER_GROUP(sb)) + 1;
    
    /* Phase 5: Load Group Descriptors */
    ────────────────────────────────────
    
    /* Calculate how many blocks needed for group descriptors */
    db_count = (sbi->s_groups_count + 
                EXT3_DESC_PER_BLOCK(sb) - 1) / 
               EXT3_DESC_PER_BLOCK(sb);
    
    sbi->s_group_desc = kmalloc(db_count * 
                                sizeof(struct buffer_head *),
                                GFP_KERNEL);
    
    /* Read each group descriptor block */
    for (i = 0; i < db_count; i++) {
        block = descriptor_loc(sb, logic_sb_block, i);
        sbi->s_group_desc[i] = sb_bread(sb, block);
        if (!sbi->s_group_desc[i]) {
            printk("EXT3: can't read group descriptor %d\n", i);
            goto failed;
        }
    }
    
    /* Validate group descriptors */
    if (!ext3_check_descriptors(sb)) {
        printk("EXT3: group descriptors corrupted\n");
        goto failed;
    }
    
    /* Phase 6: Initialize Statistics Counters */
    ─────────────────────────────────────────────
    
    percpu_counter_init(&sbi->s_freeblocks_counter,
        ext3_count_free_blocks(sb));
    percpu_counter_init(&sbi->s_freeinodes_counter,
        ext3_count_free_inodes(sb));
    percpu_counter_init(&sbi->s_dirs_counter,
        ext3_count_dirs(sb));
    
    /* Phase 7: Setup VFS Operations */
    ──────────────────────────────────
    
    sb->s_op = &ext3_sops;
    sb->s_export_op = &ext3_export_ops;
    sb->s_xattr = ext3_xattr_handlers;
    
    /* Phase 8: Journal Setup (CRITICAL for ext3) */
    ────────────────────────────────────────────────
    
    needs_recovery = (es->s_last_orphan != 0 ||
                     EXT3_HAS_INCOMPAT_FEATURE(sb,
                         EXT3_FEATURE_INCOMPAT_RECOVER));
    
    /* Load journal */
    if (EXT3_HAS_COMPAT_FEATURE(sb, 
            EXT3_FEATURE_COMPAT_HAS_JOURNAL)) {
        if (ext3_load_journal(sb, es, journal_devnum))
            goto failed;
    } else {
        printk("EXT3: No journal on filesystem\n");
        goto failed;
    }
    
    /* Validate data journaling mode with journal */
    if (!journal_check_available_features(
            sbi->s_journal, 0, 0, 
            JFS_FEATURE_INCOMPAT_REVOKE)) {
        printk("EXT3: Journal does not support data mode\n");
        goto failed;
    }
    
    /* Phase 9: Load Root Inode */
    ──────────────────────────────
    
    root = iget(sb, EXT3_ROOT_INO);
    if (!root) {
        printk("EXT3: get root inode failed\n");
        goto failed;
    }
    
    sb->s_root = d_alloc_root(root);
    if (!sb->s_root) {
        printk("EXT3: d_alloc_root failed\n");
        iput(root);
        goto failed;
    }
    
    /* Phase 10: Setup & Recovery */
    ────────────────────────────────
    
    ext3_setup_super(sb, es, sb->s_flags & MS_RDONLY);
    ext3_orphan_cleanup(sb, es);
    
    if (needs_recovery)
        printk("EXT3: recovery complete\n");
    ext3_mark_recovery_complete(sb, es);
    
    printk("EXT3: mounted filesystem with %s data mode\n",
        test_opt(sb,DATA_FLAGS) == EXT3_MOUNT_JOURNAL_DATA ?
            "journal" : "ordered");
    
    return 0;
    
failed:
    return -EINVAL;
}
```

---

## INODE & BLOCK MANAGEMENT

### Inode Reading (ext3_get_block)

```c
/* Map logical block to physical block on disk */
static int ext3_get_block(struct inode *inode, sector_t iblock,
                          struct buffer_head *bh_result,
                          int create)
{
    handle_t *handle = NULL;
    int err = -EIO;
    int offsets[4];
    Indirect chain[4];
    Indirect *partial;
    int depth;
    int blocks_to_boundary = 0;
    int indirect_blks;
    int index;
    ext3_fsblk_t goal;
    
    J_ASSERT(!(EXT3_I(inode)->i_flags & EXT3_EXTENTS_FL));
    
    depth = ext3_block_to_path(inode, iblock, offsets,
                               &blocks_to_boundary);
    if (depth == 0)
        goto out;
    
    /* Read indirect block chain from inode */
    if (!(bh_result->b_state & (1UL << BH_Mapped))) {
        /* Not yet mapped: need to find/allocate block */
        
        partial = ext3_get_branch(inode, depth, offsets,
                                  chain, &err);
        
        if (!partial) {
            /* Chain complete: block found */
            int i;
            for (i = depth - 1; i >= 0; i--)
                if (!offsets[i])
                    goto cleanup;
            
            ext3_fsblk_t first_block = le32_to_cpu(chain[depth-1].key);
            map_bh(bh_result, inode->i_sb, first_block);
            set_buffer_uptodate(bh_result);
            bh_result->b_blocknr = first_block;
            goto success;
        }
        
        /* Partial chain: some blocks missing */
        if (partial == chain) {
            /* Fetch first block from inode directly */
            goto cleanup;
        } else {
            /* Intermediate block missing */
            
            if (!create) {
                /* Read-only: can't create blocks */
                err = -ENOENT;
                goto cleanup;
            }
            
            /* Start journal transaction */
            handle = ext3_journal_start(inode,
                EXT3_DATA_TRANS_BLOCKS(inode->i_sb));
            
            if (IS_ERR(handle)) {
                err = PTR_ERR(handle);
                goto out;
            }
            
            /* Allocate blocks for chain */
            indirect_blks = (chain + depth) - partial - 1;
            goal = ext3_find_goal(inode, iblock, chain, partial);
            
            err = ext3_alloc_branch(handle, inode, indirect_blks,
                                   &goal, offsets + (partial-chain),
                                   partial);
            
            if (err) {
                ext3_journal_stop(handle);
                goto out;
            }
            
            /* Chain now complete: get mapped block */
            map_bh(bh_result, inode->i_sb,
                  le32_to_cpu(chain[depth-1].key));
            set_buffer_new(bh_result);
        }
    } else {
        /* Already mapped */
    }
    
success:
    if (handle)
        ext3_journal_stop(handle);
    return 0;
    
cleanup:
    if (handle)
        ext3_journal_stop(handle);
out:
    return err;
}

/* Traverse inode block pointers (direct + indirect) */
static ext3_fsblk_t *ext3_get_branch(struct inode *inode,
                                     int depth, int *offsets,
                                     Indirect chain[], int *err)
{
    struct super_block *sb = inode->i_sb;
    Indirect *p = chain;
    struct buffer_head *bh;
    int nr;
    
    *err = 0;
    
    /* Direct blocks: stored in inode itself */
    /* Inode has 12 direct block pointers */
    nr = offsets[0];
    if (!p->key) {
        *err = -ENOENT;
        return NULL;
    }
    
    bh = sb_bread(sb, p->key);
    if (!bh) {
        *err = -EIO;
        return p;
    }
    
    /* Single indirect: points to block with more pointers */
    /* Double indirect, Triple indirect: chains continue */
    
    for (++p, ++depth; depth--; ++p) {
        nr = le32_to_cpu(*((__le32 *)bh->b_data + offsets[depth]));
        if (!nr) {
            /* Reached end of chain */
            brelse(bh);
            return p;
        }
        
        brelse(bh);
        bh = sb_bread(sb, nr);
        if (!bh) {
            *err = -EIO;
            return p;
        }
    }
    
    brelse(bh);
    return NULL;  /* Chain complete */
}
```

### Block Allocation

```c
/* Find goal block for allocation */
static ext3_fsblk_t ext3_find_goal(struct inode *inode,
                                   ext3_lblk_t block_group,
                                   Indirect chain[], Indirect *partial)
{
    struct ext3_block_alloc_info *block_i;
    ext3_fsblk_t goal;
    
    block_i = EXT3_I(inode)->i_block_alloc_info;
    
    if (block_i && (block_i->last_alloc_logical_block + 1 == block_group)) {
        /* Use physically nearby block */
        goal = block_i->last_alloc_physical_block +
               (block_group - block_i->last_alloc_logical_block);
    } else {
        /* Find block group with free space, use first free block */
        int group = ext3_find_goal_group(inode);
        goal = (group * EXT3_BLOCKS_PER_GROUP(inode->i_sb)) +
               le32_to_cpu(EXT3_SB(inode->i_sb)->s_es->s_first_data_block);
    }
    
    return goal;
}

/* Allocate physical block(s) from free block bitmap */
ext3_fsblk_t ext3_new_block(handle_t *handle, struct inode *inode,
                            ext3_fsblk_t goal, int *errp)
{
    struct super_block *sb = inode->i_sb;
    struct ext3_sb_info *sbi = EXT3_SB(sb);
    struct ext3_group_desc *gdp;
    struct buffer_head *bitmap_bh = NULL;
    struct buffer_head *gdp_bh;
    ext3_fsblk_t ret_block;
    int group_no;
    int goal_group;
    int free_blocks;
    int bit;
    int i;
    
    *errp = -ENOSPC;
    
    /* Start at goal group */
    goal_group = (goal - le32_to_cpu(sbi->s_es->s_first_data_block)) /
                 EXT3_BLOCKS_PER_GROUP(sb);
    
    /* Find group with free blocks */
    free_blocks = le16_to_cpu(gdp->bg_free_blocks_count);
    
    for (i = 0; i < sbi->s_groups_count; i++) {
        group_no = (goal_group + i) % sbi->s_groups_count;
        gdp = ext3_get_group_desc(sb, group_no, &gdp_bh);
        
        if (!gdp) {
            *errp = -EIO;
            goto out;
        }
        
        free_blocks = le16_to_cpu(gdp->bg_free_blocks_count);
        if (free_blocks > 0)
            break;
    }
    
    if (free_blocks == 0) {
        *errp = -ENOSPC;
        goto out;
    }
    
    /* Read block bitmap for group */
    bitmap_bh = sb_bread(sb, le32_to_cpu(gdp->bg_block_bitmap));
    if (!bitmap_bh) {
        *errp = -EIO;
        goto out;
    }
    
    /* Find first free bit in bitmap */
    bit = find_goal_bit(bitmap_bh, goal % EXT3_BLOCKS_PER_GROUP(sb));
    if (bit < 0) {
        *errp = -ENOSPC;
        goto out;
    }
    
    /* Mark bit as used in bitmap */
    ext3_debug("allocating block %lu\n", ret_block);
    
    ext3_set_bit(bit, bitmap_bh->b_data);
    
    /* Mark bitmap buffer dirty for journal */
    BUFFER_TRACE(bitmap_bh, "marking dirty");
    mark_buffer_dirty(bitmap_bh);
    
    /* Update group descriptor counters */
    gdp->bg_free_blocks_count = 
        cpu_to_le16(le16_to_cpu(gdp->bg_free_blocks_count) - 1);
    
    /* Calculate physical block number */
    ret_block = (ext3_fsblk_t)group_no * EXT3_BLOCKS_PER_GROUP(sb) +
               le32_to_cpu(sbi->s_es->s_first_data_block) + bit;
    
    /* Update filesystem counters */
    percpu_counter_dec(&sbi->s_freeblocks_counter);
    
    *errp = 0;
    return ret_block;
    
out:
    brelse(bitmap_bh);
    return 0;
}
```

---

## DATA FLOW & I/O PATH

### Read Path (userspace read())

```
1. Application Code:
   bytes_read = read(fd, buffer, 4096)
   
   ↓ [enter kernel]
   
2. System Call Entry:
   sys_read(fd, buffer, 4096)
   └─ get_fd_from_current_task(fd) → file struct
   └─ call file->f_op->read(file, buffer, 4096, &offset)
   
3. VFS Read Handler:
   generic_file_read_iter(file, iov_iter, offset)
   └─ file_read_actor() [for sequential reads]
       └─ do_generic_file_read()
   
4. Page Cache Lookup:
   find_get_page(address_space, offset >> PAGE_SHIFT)
   ├─ Check radix_tree for cached page
   ├─ If hit: ✓ Page already in memory (from previous read)
   │  └─ copy_to_user(buffer, page->virtual, count)
   │  └─ return count
   ├─ If miss: ✗ Need disk I/O
   │  └─ page_cache_read(file, index)
   │     └─ page = find_or_create_page(mapping, index)
   │     └─ mapping->a_ops->readpage(file, page)
   │        └─ ext3_readpage(file, page)
   │           └─ ext3_get_block() [maps logical to physical]
   │           └─ submit_bh(READ, bh) [issue I/O]
   │        └─ wait_on_page_locked(page) [wait for disk]
   │     └─ SetPageUptodate(page)
   │  └─ copy_to_user(buffer, page->virtual, count)
   └─ return count

5. Block Layer (submit_bh):
   submit_bh(READ, buffer_head)
   └─ bio_alloc(GFP_NOIO, 1)
   └─ bio->bi_sector = buffer_head->b_blocknr
   └─ bio->bi_bdev = buffer_head->b_bdev
   └─ submit_bio(READ, bio)
       └─ __make_request(queue, bio)
           ├─ Try merge with adjacent request
           │  └─ ELEVATOR_BACK_MERGE: append to existing
           │  └─ ELEVATOR_NO_MERGE: create new request
           ├─ elevator->add_request(request)
           │  └─ CFQ scheduler maintains per-process queue
           │  └─ Coalesce/reorder for disk efficiency
           └─ queue->request_fn(queue)
               └─ ata_request_fn()
   
6. Device Driver (ATA/SATA):
   ata_request_fn(queue)
   ├─ elv_next_request(queue) → get request
   ├─ Build ATA command:
   │  ├─ sector = request->sector
   │  ├─ nr_sectors = request->nr_sectors
   │  └─ command = ATA_CMD_READ_DMA
   ├─ Setup DMA:
   │  └─ ata_sg_init() [scatter-gather for pages]
   │  └─ prde_build() [build physical region descriptors]
   ├─ Issue command:
   │  ├─ outb(ATA_CMD_READ_DMA, command_port)
   │  ├─ outl(prde_addr, dma_addr_port)
   │  └─ outb(DMA_START, dma_control_port)
   └─ Request marked as in-flight
   
7. Storage Hardware:
   SATA disk reads data from physical sectors
   └─ Data transferred via DMA to RAM
   └─ Sets completion status in device register
   └─ Asserts interrupt line
   
8. Interrupt Handler:
   IRQ Handler (ata_intr)
   ├─ Check device status register
   ├─ If error: mark request with error
   ├─ If success: mark request complete
   ├─ ata_qc_complete(ata_cmd)
   │  └─ bio->bi_end_io(bio, uptodate) [callback]
   │  └─ end_buffer_async_read()
   │     └─ SetPageUptodate(page)
   │     └─ unlock_page(page)
   │     └─ wake_up(&page->wait_queue)
   └─ queue->request_fn(queue) [process next request]
   
9. Back in VFS:
   wait_on_page_locked(page) woken up
   └─ Page now has data from disk
   
10. Copy to User:
    copy_to_user(buffer, page->virtual, count)
    └─ Copy data from page cache to user buffer
    └─ Handle page fault if user buffer not resident
    
11. Return to Application:
    read() returns count of bytes read
```

### Write Path (userspace write())

```
1. Application:
   bytes_written = write(fd, buffer, 4096)
   
2. System Call:
   sys_write(fd, buffer, 4096)
   └─ generic_file_write_iter()
   
3. VFS Write:
   generic_file_write_iter()
   ├─ get_write_access(file)
   ├─ file_update_time(file)
   └─ __generic_file_aio_write()
       └─ generic_file_buffered_write()
           └─ file->f_mapping->a_ops->write_begin()
               └─ ext3_write_begin(file, mapping, pos, len, flags, pagep, fsdata)
                   ├─ page = grab_cache_page_write_begin(mapping, index)
                   │  └─ page = find_or_create_page(mapping, index)
                   │  └─ If new page: may trigger read-ahead
                   ├─ ext3_get_block(inode, lblock, &bh, create=1)
                   │  └─ Map logical block to physical
                   │  └─ If new block: allocate from free space
                   ├─ ext3_journal_start() [start transaction]
                   └─ *pagep = page [return to caller]
           
4. Copy User Data:
   copy_from_user(page->virtual, buffer, len)
   └─ Copy application buffer to page cache
   └─ May trigger page fault if page not resident
   
5. Filesystem Write Callback:
   file->f_mapping->a_ops->write_end()
   └─ ext3_write_end()
       ├─ set_page_dirty(page)
       │  └─ Mark page as needing writeback
       ├─ mark_buffer_dirty(bh)
       │  └─ Mark buffer_head as dirty
       ├─ ext3_journal_stop()
       │  └─ Commit metadata changes to journal
       └─ unlock_page(page)
   
6. Mark Dirty:
   set_page_dirty(page)
   └─ __mark_inode_dirty(inode, I_DIRTY_DATASYNC)
       ├─ Add inode to dirty list
       ├─ Increment s_dirty counter
       └─ Wake write-back daemon if over threshold
   
7. Write-Back Daemon (pdflush/kthreadd):
   balance_dirty_pages()
   ├─ Check: dirty_pages > dirty_background_ratio
   │  └─ Wake write-back thread if true
   ├─ Check: dirty_pages > dirty_ratio
   │  └─ Block further writes if limit reached
   └─ writeback_inodes() [sync dirty inodes to disk]
       └─ For each dirty inode:
           └─ inode->i_mapping->a_ops->writepages()
               └─ ext3_writepages()
                   └─ write_cache_pages()
                       └─ For each dirty page:
                           └─ page->mapping->a_ops->writepage()
                               └─ ext3_writepage()
                                   ├─ For each buffer on page:
                                   │  ├─ if dirty: submit_bh(WRITE, bh)
                                   │  └─ WRITE_BARRIER if ordered-mode
                                   └─ Lock page (wait for all BH complete)
   
8. Block Layer:
   submit_bh(WRITE, buffer_head)
   └─ submit_bio(WRITE, bio)
       └─ __make_request()
           ├─ Merge with adjacent write requests if possible
           │  └─ CFQ: Maintains write queue for fairness
           ├─ Insert into I/O scheduler
           │  └─ May reorder for disk efficiency
           └─ queue->request_fn()
   
9. Device Driver:
   ata_request_fn()
   ├─ Build ATA WRITE_DMA command
   ├─ Setup DMA descriptors pointing to data pages
   ├─ Issue command to disk
   └─ In-flight request waiting for completion
   
10. Storage Hardware:
    SATA disk receives data from RAM via DMA
    └─ Writes data to physical sectors
    └─ Asserts completion interrupt
    
11. Interrupt Completion:
    ata_intr() [IRQ handler]
    └─ Mark request complete
    └─ Call end_io callback
    └─ SetPageUptodate(page) [cache is now consistent]
    └─ ClearPageWriteback(page)
    └─ wake_up(&inode->i_waitq)
    
12. Return to Application:
    write() returns count of bytes written
    
Note: Data is NOT guaranteed on disk until:
  - sync() system call
  - fsync(fd)
  - Journal commit timer expires
```

---

## COMPLETE EXECUTION TRACE

### Full Read I/O Request from User→Hardware→User

```
SCENARIO: User calls read(fd, buf, 4096) on ext3 file at byte 8192

TIME=0ms:
  ┌─────────────────────────────────────────────────────┐
  │ User Space Application                              │
  │ Process: nginx (PID 1234)                          │
  │ Instruction: mov $1 %eax; syscall                  │
  │             (syscall: sys_read)                    │
  └─────────────────────────────────────────────────────┘
           ↓ [context switch to kernel]

TIME=0.01ms:
  ┌─────────────────────────────────────────────────────┐
  │ Kernel Entry Point (arch/x86/entry.S)              │
  │ Vector: INT 0x80 / SYSCALL                         │
  │ Handler: system_call → sys_read()                  │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=0.05ms:
  ┌─────────────────────────────────────────────────────┐
  │ VFS Layer (fs/read_write.c)                        │
  │ vfs_read()                                          │
  │ ├─ file = fget(fd) [get file struct]              │
  │ ├─ f_pos = 8192                                    │
  │ └─ call file->f_op->read()                         │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=0.1ms:
  ┌─────────────────────────────────────────────────────┐
  │ Generic File Read (mm/filemap.c)                   │
  │ generic_file_read_iter()                           │
  │ ├─ index = f_pos >> PAGE_SHIFT = 2                 │
  │ │           (assuming 4KB pages: 8192>>12 = 2)    │
  │ ├─ find_get_page(mapping, index=2)                │
  │ │  └─ Check page cache radix tree                  │
  │ │  └─ Result: NOT FOUND (cache miss!)             │
  │ ├─ page = page_cache_alloc(mapping)               │
  │ │  └─ Allocate new page from buddy allocator      │
  │ └─ mapping->a_ops->readpage(file, page)           │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=0.15ms:
  ┌─────────────────────────────────────────────────────┐
  │ EXT3 Readpage (fs/ext3/inode.c)                    │
  │ ext3_readpage(file, page)                          │
  │ ├─ page->index = 2 (page offset in file)           │
  │ ├─ for each buffer_head on page:                   │
  │ │  ├─ iblock = (page->index << (PAGE_SHIFT - sb->  │
  │ │  │            s_blocksize_bits))                 │
  │ │  │         = (2 << (12 - 12)) = 2               │
  │ │  │         (logical block 2 in file)             │
  │ │  ├─ ext3_get_block(inode, iblock=2, &bh,        │
  │ │  │                create=0)                      │
  │ │  └─ Block lookup via indirect blocks:            │
  │ │      inode: [D0=5] [D1=6] ... [IND1] [DBL] [TRP]│
  │ │      For logical block 2 (in direct zone):       │
  │ │        physblock = inode->i_block[2] = 47       │
  │ │  ├─ bh->b_blocknr = 47                           │
  │ │  └─ set_buffer_mapped(bh)                        │
  │ │  └─ submit_bh(READ, bh)                          │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=0.2ms:
  ┌─────────────────────────────────────────────────────┐
  │ Buffer Cache (fs/buffer.c)                         │
  │ submit_bh(READ, bh)                                │
  │ ├─ lock_buffer(bh)                                 │
  │ │  └─ Set BH_Lock flag                             │
  │ ├─ bio = bio_alloc(GFP_NOIO, 1)                   │
  │ │  └─ Allocate bio structure                       │
  │ ├─ bio setup:                                       │
  │ │  ├─ bi_sector = 47 (physical block * 8)         │
  │ │  │          = 47 * 8 = 376 (512-byte sectors)  │
  │ │  ├─ bi_bdev = /dev/sda1                         │
  │ │  ├─ bi_io_vec[0].bv_page = page                │
  │ │  ├─ bi_io_vec[0].bv_offset = 0                 │
  │ │  ├─ bi_io_vec[0].bv_len = 4096                 │
  │ │  ├─ bi_vcnt = 1                                 │
  │ │  ├─ bi_size = 4096                             │
  │ │  ├─ bi_end_io = end_buffer_async_read          │
  │ │  └─ bi_private = bh                             │
  │ └─ submit_bio(READ, bio)                           │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=0.25ms:
  ┌─────────────────────────────────────────────────────┐
  │ Block I/O Layer (block/ll_rw_blk.c)               │
  │ generic_make_request(bio)                          │
  │ ├─ q = bdev_get_queue(/dev/sda1)                  │
  │ │  └─ Return request_queue for sda1               │
  │ ├─ __make_request(q, bio)                          │
  │ │  ├─ elv_merge(q, &req, bio)                     │
  │ │  │  └─ CFQ: Check if can merge with existing    │
  │ │  │  └─ Result: ELEVATOR_NO_MERGE (no match)    │
  │ │  ├─ get_request(q, READ, bio)                   │
  │ │  │  └─ Allocate struct request from pool        │
  │ │  ├─ request setup:                               │
  │ │  │  ├─ rq->bio = bio                            │
  │ │  │  ├─ rq->sector = 376                         │
  │ │  │  ├─ rq->nr_sectors = 8 (4096 bytes)          │
  │ │  │  ├─ rq->cmd_type = REQ_TYPE_FS               │
  │ │  │  └─ rq->cmd_flags = READ                     │
  │ │  ├─ elv_insert(q, rq, ELEVATOR_INSERT_SORT)    │
  │ │  │  └─ CFQ scheduler: Insert into per-process   │
  │ │  │     queue with fairness consideration        │
  │ │  └─ if (!queue_plugged(q)):                     │
  │ │     └─ q->request_fn(q) [IMMEDIATE DISPATCH]   │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=0.3ms:
  ┌─────────────────────────────────────────────────────┐
  │ Device Driver (drivers/ata/libata-core.c)          │
  │ ata_request_fn(queue)                              │
  │ ├─ spin_lock_irqsave(&ata_port->lock)             │
  │ ├─ rq = elv_next_request(queue)                   │
  │ │  └─ CFQ: Dequeue next request with fairness    │
  │ ├─ ata_sg_init(rq, ATA_CMD_READ_DMA)             │
  │ │  └─ Set up scatter-gather for DMA transfer     │
  │ ├─ ATA command construction:                       │
  │ │  ├─ lba_low = 376 & 0xFF = 120                 │
  │ │  ├─ lba_mid = (376 >> 8) & 0xFF = 1            │
  │ │  ├─ lba_high = (376 >> 16) & 0xFF = 0          │
  │ │  ├─ device = ((376 >> 24) & 0xF) | 0xE0        │
  │ │  ├─ count = 8 sectors                           │
  │ │  └─ command = 0xC8 (READ_DMA)                   │
  │ ├─ DMA setup:                                       │
  │ │  ├─ prd = build_prdt_entries(rq)               │
  │ │  │  └─ Create Physical Region Descriptors       │
  │ │  │  └─ Address: virtual page → physical DMA addr│
  │ │  ├─ outl(prd_addr, ATA_DMA_TABLE_OFS)          │
  │ │  │  └─ Write DMA table address to device        │
  │ │  └─ DMA direction: device → memory              │
  │ ├─ Command issue:                                  │
  │ │  ├─ outb(0x08, ATA_CMD_PORT)  /* READ_DMA */   │
  │ │  └─ Request now IN-FLIGHT, waiting for disk    │
  │ └─ spin_unlock_irqrestore(&ata_port->lock)       │
  └─────────────────────────────────────────────────────┘
           ↓ [Hardware READ begins...]

TIME=1-3ms: [DISK I/O IN PROGRESS]
  Physical disk reads 8 sectors (4KB) from sectors 376-383
  └─ Mechanical: Seek to cylinder, wait for rotation, read
  └─ SSD: Access flash storage, return data
  └─ Data loaded into DMA buffer in RAM
  └─ SATA device asserts interrupt when complete

TIME=3.5ms:
  ┌─────────────────────────────────────────────────────┐
  │ Interrupt Handler (arch/x86/kernel/irq.c)         │
  │ do_IRQ(irq=31) [SATA device interrupt]            │
  │ └─ Call registered handler: ata_intr()            │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=3.6ms:
  ┌─────────────────────────────────────────────────────┐
  │ SATA Driver Interrupt (drivers/ata/libata-core.c)  │
  │ ata_intr(irq=31, dev_instance=ata_host)            │
  │ ├─ for each ata_port in host:                      │
  │ │  ├─ status = inb(ATA_STATUS_PORT)               │
  │ │  ├─ if (status & ATA_BUSY): continue (not done) │
  │ │  ├─ if (status & (ATA_ERR | ATA_DF)):           │
  │ │  │  └─ err_mask |= AC_ERR_DEV                   │
  │ │  ├─ else: [SUCCESS]                              │
  │ │  │  └─ err_mask = 0                              │
  │ │  ├─ ata_qc_complete(ap->qc_active[i])           │
  │ │  │  └─ Completion handler                        │
  │ │  └─ queue->request_fn(queue) [next request]     │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=3.65ms:
  ┌─────────────────────────────────────────────────────┐
  │ ATA Completion (libata-core.c)                      │
  │ ata_qc_complete(ata_queued_cmd)                     │
  │ ├─ ata_qc_end_in_progress_cb()                      │
  │ ├─ if (!err): ata_sg_clean(qc)                     │
  │ │  └─ DMA transfer complete, buffers valid        │
  │ ├─ rq->errors = err_mask                           │
  │ ├─ Call bio completion:                            │
  │ │  ├─ bio = rq->bio                               │
  │ │  ├─ bio->bi_end_io(bio, uptodate=1)             │
  │ │  │  └─ end_buffer_async_read(bio, 1)            │
  │ │  │     ├─ bh = bio->bi_private                  │
  │ │  │     ├─ set_buffer_uptodate(bh)               │
  │ │  │     │  └─ Set BH_Uptodate flag               │
  │ │  │     ├─ unlock_buffer(bh)                      │
  │ │  │     │  └─ Clear BH_Lock, wake_up(&bh->wait) │
  │ │  │     └─ put_bh(bh) [release ref]              │
  │ │  ├─ SetPageUptodate(page) [page has valid data]│
  │ │  └─ unlock_page(page) [wake page readers]      │
  │ └─ __blk_run_queue(q) [dispatch next if queued]   │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=3.7ms: [Back to VFS I/O wait]
  ┌─────────────────────────────────────────────────────┐
  │ Wait Queue Wakeup                                   │
  │ wait_on_page_locked(page) [in ext3_readpage]       │
  │ └─ WOKEN UP: page has data                         │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=3.75ms:
  ┌─────────────────────────────────────────────────────┐
  │ Back in Generic File Read (filemap.c)              │
  │ do_generic_file_read() continues                    │
  │ ├─ page is now in cache with valid data            │
  │ ├─ file_read_actor(desc, page, offset, size)       │
  │ │  └─ copy_to_user(user_buffer, page->virtual,    │
  │ │                  4096)                           │
  │ │     ├─ Disable preemption                        │
  │ │     ├─ Check user buffer is writable             │
  │ │     ├─ memcpy() kernel→user page-by-page        │
  │ │     └─ Re-enable preemption                      │
  │ └─ Return bytes_copied = 4096                      │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=3.8ms:
  ┌─────────────────────────────────────────────────────┐
  │ VFS Read Return (fs/read_write.c)                  │
  │ vfs_read() returns bytes_read = 4096               │
  │ └─ sys_read() continues                            │
  └─────────────────────────────────────────────────────┘
           ↓

TIME=3.85ms:
  ┌─────────────────────────────────────────────────────┐
  │ Kernel Exit (arch/x86/entry.S)                     │
  │ SYSRET / IRET instruction                           │
  │ └─ Return to user space                             │
  │ └─ RAX = 4096 (bytes read)                         │
  └─────────────────────────────────────────────────────┘
           ↓ [context switch to user]

TIME=3.9ms:
  ┌─────────────────────────────────────────────────────┐
  │ User Space Application                              │
  │ Process: nginx (PID 1234)                          │
  │ read() syscall returns: eax = 4096                 │
  │ Buffer at 0x7fff1234 now contains 4KB of data      │
  │ Application continues with next instruction        │
  └─────────────────────────────────────────────────────┘

SUMMARY:
  User request: 1 syscall
  VFS layer: 1 readpage operation
  Buffer/BIO: 1 BIO submission
  I/O Scheduler: 1 request queued & dispatched
  Device Driver: 1 ATA command issued
  Hardware: ~2-3ms disk access time
  Interrupt: 1 completion interrupt
  Total Time: ~3.9ms from syscall entry to return
```

---

## PERFORMANCE CHARACTERISTICS

### Latency Breakdown (Approximate)

```
Cached read (page already in memory):
  VFS lookup:     0.01ms
  Page cache hit:  0.05ms
  copy_to_user:    0.10ms
  ─────────────────────
  Total:           0.16ms

Uncached read (page from disk):
  Syscall entry:   0.01ms
  VFS setup:       0.05ms
  EXT3 get_block:  0.10ms (block mapping)
  BIO creation:    0.05ms
  I/O Scheduler:   0.05ms
  Device dispatch: 0.05ms
  ─────────────────────
  Disk time:       2-10ms (varies)
  ─────────────────────
  Interrupt/wakeup: 0.05ms
  VFS completion:   0.05ms
  copy_to_user:     0.10ms
  Syscall exit:     0.01ms
  ─────────────────────
  Total:           2-11ms (disk-dependent)
```

### I/O Scheduler Impact (CFQ vs Deadline vs NOOP)

```
CFQ (default for rotation):
  ├─ Per-process fairness queues
  ├─ Read/write separation
  ├─ Anticipation: Wait for sequential next request
  ├─ Merging: Back & front merge
  └─ Good for: Mixed workloads, fairness
  └─ Latency: +5-20% vs deadline for random
  └─ Throughput: Better on workstations

DEADLINE:
  ├─ Single sorted queue + deadline list
  ├─ Expires requests after timeout
  ├─ Front merge priority
  └─ Good for: Storage servers, predictable latency
  └─ Latency: Better than CFQ for latency-critical
  └─ Throughput: Comparable to CFQ

NOOP:
  ├─ Simple FIFO queue
  ├─ No reordering, no merging  
  ├─ Fast insert: O(1)
  └─ Good for: SSDs, virtual storage (where disk doesn't matter)
  └─ Latency: Worse on rotating media
  └─ Throughput: Better on SSD than complex schedulers
```

---

## SUMMARY: Complete Flow

```
┌──────────────────────────────────────────────────────────┐
│ 1. APPLICATION LAYER (User Space)                        │
│    Application calls read(), write(), or fsync()         │
└────────────┬─────────────────────────────────────────────┘
             │ syscall (INT 0x80 / SYSCALL)
             ↓
┌──────────────────────────────────────────────────────────┐
│ 2. VFS LAYER (Kernel)                                    │
│    - Generic read/write operations                       │
│    - Page cache management                               │
│    - Inode/dentry traversal                             │
└────────────┬─────────────────────────────────────────────┘
             │ address_space_operations callbacks
             ↓
┌──────────────────────────────────────────────────────────┐
│ 3. FILESYSTEM LAYER (ext3)                               │
│    - Superblock, group descriptor reading                │
│    - Inode operations                                    │
│    - Block mapping (ext3_get_block)                      │
│    - Journal management                                  │
└────────────┬─────────────────────────────────────────────┘
             │ buffer head I/O operations
             ↓
┌──────────────────────────────────────────────────────────┐
│ 4. BUFFER CACHE & BIO LAYER                              │
│    - Buffer head allocation                              │
│    - BIO creation and submission                         │
│    - Request creation                                    │
└────────────┬─────────────────────────────────────────────┘
             │ submit_bio() → __make_request()
             ↓
┌──────────────────────────────────────────────────────────┐
│ 5. I/O SCHEDULER (elevator algorithm)                    │
│    - Request merging                                     │
│    - Request reordering                                  │
│    - Fairness enforcement                                │
└────────────┬─────────────────────────────────────────────┘
             │ request_fn() dispatch
             ↓
┌──────────────────────────────────────────────────────────┐
│ 6. BLOCK DEVICE DRIVER (ATA/SATA/SCSI)                  │
│    - Command construction                                │
│    - DMA buffer setup                                    │
│    - Hardware register programming                       │
└────────────┬─────────────────────────────────────────────┘
             │ I/O request sent to device
             ↓
┌──────────────────────────────────────────────────────────┐
│ 7. STORAGE HARDWARE                                      │
│    - Read/write from disk/SSD                           │
│    - DMA data transfer                                   │
│    - Interrupt on completion                             │
└────────────┬─────────────────────────────────────────────┘
             │ Interrupt line asserted
             ↓
┌──────────────────────────────────────────────────────────┐
│ 8. INTERRUPT HANDLER (device driver)                     │
│    - Status/error checking                               │
│    - Completion notification                             │
│    - Next request dispatch                               │
└────────────┬─────────────────────────────────────────────┘
             │ Wake waiting processes
             ↓
┌──────────────────────────────────────────────────────────┐
│ 9. BUFFER/BIO COMPLETION                                 │
│    - Mark buffers uptodate                               │
│    - Page cache consistency                              │
│    - Unlock page/buffer                                  │
└────────────┬─────────────────────────────────────────────┘
             │ Wake page waiters
             ↓
┌──────────────────────────────────────────────────────────┐
│ 10. VFS COMPLETION                                       │
│     - Return to syscall handler                          │
│     - copy_to_user() data to user buffer                 │
│     - Return bytes read/written                          │
└────────────┬─────────────────────────────────────────────┘
             │ Return from syscall
             ↓
┌──────────────────────────────────────────────────────────┐
│ 11. APPLICATION (back in user space)                     │
│     Application continues with next instruction          │
│     Data in user buffer ready for processing             │
└──────────────────────────────────────────────────────────┘
```

---

**Document Version**: 1.0  
**Linux Kernel**: 2.6.20  
**Filesystem**: ext2/ext3  
**Last Updated**: 2024  

