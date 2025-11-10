# Why do we need the slab allocator?
The buddy allocator (lower layer) only allocates memory in sizes of 2^order × PAGE_SIZE, e.g.:
1 page (4 KB)
2 pages
4 pages
8 pages
16 pages
etc.

Using buddy allocator for those would cause:
huge internal fragmentation
slow allocations
expensive initialization
no per-CPU optimization
So the slab allocator fixes all that.

# Core Concepts of Slab Allocator
1. Cache (kmem_cache)
A “cache” stores objects of one type and one size.
Examples:
kmem_cache for: inode_cache
kmem_cache for: dentry_cache
kmem_cache for: kmalloc-64
kmem_cache for: kmalloc-128
Each cache:
knows the object size
keeps a list of slabs
has constructor/destructor for objects
has per-CPU fast data structures

2. Slabs
A slab is a group of pages (typically 1–8 pages) allocated from buddy allocator.
Inside the slab:
memory is divided into objects of fixed size
slab has a freelist (free objects)
slab has metadata (struct slab)
A slab can be:
FULL       – all objects in use
PARTIAL    – some used, some free
EMPTY      – all free
Slabs are stored in three lists in the cache:
cache->slabs_full
cache->slabs_partial
cache->slabs_free

3. Objects
Each slab contains many objects:
Slab of 8KB, object = 64 bytes
→ contains 128 objects
Each object can be quickly allocated or freed using a freelist.

# How slab allocation works
kmalloc(64)
  ↓
kmalloc-64 cache
  ↓
kmem_cache_alloc()
  ↓
Check per-CPU cache (fast path)
  ↓
If empty → check partial slabs
  ↓
If no partial → allocate new slab
  ↓
carve slab pages into objects
  ↓
give one free object to caller



# Flow Diagram of the Slab Allocator (detailed)
                   kmalloc(size)
                        │
                        ▼
               kmalloc caches choose a cache
                        │
                        ▼
               kmem_cache_alloc(cache)
                        │
                        ▼
         ┌──────────────────────────────────────┐
         │ FAST PATH: per-CPU magazine free?    │
         └──────────────────────────────────────┘
                │ yes                 │ no
                ▼                      ▼
         return object        pull objects from slab
                                       │
                                       ▼
                     ┌────────────────────────────────┐
                     │ Does cache have partial slab?  │
                     └────────────────────────────────┘
                             │ yes           │ no
                             ▼               ▼
                  Allocate from that   New slab allocation
                     partial slab            │
                                             ▼
                      allocate pages from buddy allocator
                                             │
                                             ▼
                             Initialize slab metadata
                           (freelist, constructor, etc.)
                                             │
                                             ▼
                                 allocate object


# How freeing works
                 kmalloc() calls slab allocator
                             │
                             ▼
                   +----------------+
                   | slab allocator |
                   +----------------+
                     /    |       \
                    /     |        \
            object cache  |         slab of pages
                           \
                            ▼
                 buddy allocator (alloc_pages)
                            │
                            ▼
                      physical pages

# slab for kmalloc-128
kmalloc-128 cache:
------------------------
object size = 128 bytes
slab size   = 4096 bytes
objects/slab = 4096/128 = 32

Slab layout:
----------------------------------------------
| obj0 | obj1 | obj2 | ... | obj31 | metadata |
----------------------------------------------

Metadata contains freelist.

