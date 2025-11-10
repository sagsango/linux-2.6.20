                          kmalloc(size)
                                │
                                ▼
                SLUB? No → slab allocator (2.6.20 uses SLAB)
                                │
                                ▼
              Determine size class → kmalloc_index(size)
                                │
                                ▼
               Select kmalloc cache:
                   kmalloc-32
                   kmalloc-64
                   kmalloc-128
                   kmalloc-256
                   ...
                                │
                                ▼
                      cache = kmalloc_caches[index]
                                │
                                ▼
                       kmem_cache_alloc(cache, flags)
                                │
                                ▼
         +--------------------------------------------------+
         |  Are there free objects in this CPU’s magazine?  |
         +--------------------------------------------------+
                  │ yes                             │ no
                  ▼                                   ▼
      Return object from per-CPU cache     Fetch new slab or objects
                                                      │
                                                      ▼
                                   +---------------------------------------+
                                   | Does cache have a slab with free objs? |
                                   +---------------------------------------+
                                               │ yes        │ no
                                               ▼            ▼
                            Allocate from slab freelist     Create new slab
                                                             │
                                                             ▼
                                                +---------------------------+
                                                | Allocate backing pages     |
                                                | from buddy allocator       |
                                                | (order depending on slab)  |
                                                +---------------------------+
                                                             │
                                                             ▼
                                              Initialize slab + freelist
                                                             │
                                                             ▼
                                             Allocate object from slab
                                │
                                ▼
                            return ptr

