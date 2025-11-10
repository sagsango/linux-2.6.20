# What page_io.c Is
This file implements low-level I/O for swapping pages between:
Physical RAM
Swap device (swap partition or swapfile)
It uses the block I/O layer (BIO) to read/write swap pages asynchronously.
This is the internal path when the kernel needs to:
- write a RAM page → swap disk
- read a swapped-out page → RAM
- remove swapcache pages
- initiate read/write requests to block devices via BIO
This file is the glue between:

Page Cache  <→>  Swap Subsystem  <→>  Block Layer (bio)  <→>  Disk


# get_swap_bio() — Build BIO for 1-page swap I/O
                        get_swap_bio()
                                │
                                ▼
                    bio = bio_alloc(gfp, 1 vec)
                                │
                                ▼
                        Create swp_entry_t
                                │
                                ▼
                sis = get_swap_info_struct(type)
                                │
                                ▼
                sector = map_swap_page(sis, offset)
                                │
                                ▼
      bio fields setup:
            bi_sector ← disk sector for swap page
            bi_bdev   ← swap device blockdev
            bi_io_vec[0]:
                   page   = target page
                   len    = PAGE_SIZE
                   offset = 0
            bi_end_io = callback
                                │
                                ▼
                          return bio

# end_swap_bio_write() — Callback for WRITE completion
end_swap_bio_write(bio)
        │
        ├── if (bio->bi_size != 0) → not done yet → return 1
        │
        ▼
Check if BIO_UPTODATE bit is set?
        │
        ├── NO (I/O error)
        │      │
        │      ├─ SetPageError(page)
        │      ├─ set_page_dirty(page)
        │      ├─ ClearPageReclaim(page)
        │      └─ printk error
        │
        └── YES
               (write good)
        │
        ▼
end_page_writeback(page)
        │
bio_put(bio)
        ▼
return 0


# end_swap_bio_read() — Callback for READ completion
end_swap_bio_read(bio)
        │
        ├── if (bio->bi_size != 0) → not done → return 1
        │
        ▼
Check if BIO_UPTODATE?
        │
        ├── NO:
        │      SetPageError(page)
        │      ClearPageUptodate(page)
        │      printk error
        │
        └── YES:
               SetPageUptodate(page)
        │
        ▼
unlock_page(page)
        │
bio_put(bio)
        ▼
return 0


# swap_writepage() — Write a page to swap
swap_writepage(page)
        │
        ▼
remove_exclusive_swap_page?
        │
        ├── YES: this page is stale
        │      unlock page
        │      return 0
        │
        ▼
bio = get_swap_bio(...)
        │
        ├── NULL → out of memory → re-dirty page → return -ENOMEM
        │
        ▼
SetPageWriteback(page)
unlock_page(page)
submit_bio(WRITE, bio)
return 0

# swap_readpage() — Read swap page into memory
swap_readpage()
        │
        ▼
bio = get_swap_bio(...)
        │
        ├── NULL → return -ENOMEM
        │
        ▼
ClearPageUptodate(page)
submit_bio(READ, bio)
return 0

# Swap Out
RAM page → swap_writepage()
             │
             ▼
       get_swap_bio()
             │
             ▼
      submit_bio(WRITE)
             │
             ▼
  Disk driver → disk (swap area)
             │
             ▼
  end_swap_bio_write()
             │
             ▼
  page writeback complete


# Swap In
Process page fault on swapped-out page
             │
             ▼
        swap_readpage()
             │
             ▼
        get_swap_bio()
             │
             ▼
      submit_bio(READ)
             │
             ▼
  Disk driver → disk (swap area)
             │
             ▼
end_swap_bio_read()
             │
             ▼
page becomes Uptodate + unlocked → usable

# How swap I/O fits in whole MM system 
User process needs page
    │
    ▼
page fault → lookup swap entry
    │
    ▼
swap_readpage() → get_swap_bio → submit_bio
    │
    ▼
disk I/O → page returned to memory
    │
    ▼
process continues

Memory pressure situation:
    │
    ▼
shrink_inactive_list() → try_to_swap()
    │
    ▼
swap_writepage()
    │
    ▼
get_swap_bio → submit_bio(WRITE)


