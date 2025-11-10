# readahead.c
Userspace read() syscall
        │
        ▼
page_cache_readahead(mapping, ra, filp, offset, req_size)
        │
        ▼
Adaptive algorithm decides:
    - sequential?
    - random?
    - first read?
    - expand window?
    - reduce window?
        │
        ▼
Compute new readahead window:
    ra->start, ra->size
    ra->ahead_start, ra->ahead_size
        │
        ▼
Submit readahead I/O:
    blockable_page_cache_readahead()
        │
        ▼
__do_page_cache_readahead()
        │
        ▼
Allocate pages (cold pages)
        │
        ▼
Add pages to page cache
        │
        ▼
mapping->a_ops->readpage(s)  → submit BIO READ
        │
        ▼
Block layer → Disk driver → Disk
        │
        ▼
Pages fill into page cache


# __do_page_cache_readahead(): REAL WORK
__do_page_cache_readahead(offset, nr_to_read)
        │
        ▼
for each page in window:
        │
        ├─ if already in pagecache → skip
        │
        ├─ Allocate new page (cold)
        │
        └─ Add to temporary list (page_pool)
        ▼
read_pages(mapping, filp, &page_pool, count)
        │
        ▼
a_ops->readpages or loop → a_ops->readpage()
        │
        ▼
Filesystem prepares bio read request
        │
submit_bio(READ)
        │
        ▼
I/O scheduler → Disk driver → disk



# Detailed flow
                Userspace read() syscall
                          │
                          ▼
            page_cache_readahead(mapping, ra, filp, offset, req_size)
                          │
        ┌─────────────────┴────────────────┐
        │                                  │
Sequential?                         Random access?
        │                                  │
        ▼                                  ▼
Check window                           ra_off()
Adjust window                           Do minimal I/O
Build ahead window                      return
Submit I/O
        │
        ▼
blockable_page_cache_readahead()
        │
        ▼
__do_page_cache_readahead()
        │
        ▼
Allocate missing pages
        │
        ▼
Add to page cache
        │
        ▼
read_pages()
        │
        ▼
Filesystem readpage(s)
        │
        ▼
submit_bio(READ)
        │
        ▼
Disk driver → Disk
        │
        ▼
Pages in cache → next read() hits memory

