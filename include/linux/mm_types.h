#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/types.h>
#include <linux/threads.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct address_space;

/*
 * XXX:
 *	Page
 *	Page cache has pages for the file data cache.
 *	Page will be allocated to mm_struct->pagedir too, but they are not part of the
 *	mm_struct has vm_area_struct too, they are just metadata about address-ranges
 *	cache (if not file backed)
 *
 *
 *	If we mmap a file in shared mode;
 *	all the data of the file will be present in the page cache.
 *	so we will just get all the pages present in the address_space and map
 *	those pages to the continous address in the page-table, and metadata
 *	of the mapping will be present in the vma.
 *
 *	if we map a file in private mode.
 *	we will just copy all the pages from address_space which are
 *	from the page cache, and create new pages in page-table by copy.
 */
/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page, though if it is a pagecache page, rmap structures can tell us
 * who is mapping it.
 */
struct page {
	unsigned long flags;		/* Atomic flags, some possibly
					 * updated asynchronously */
	atomic_t _count;		/* Usage count, see below. */
	atomic_t _mapcount;		/* Count of ptes mapped in mms,
					 * to show when page is mapped
					 * & limit reverse map searches.
					 */
	union {
	    struct {
		unsigned long private;		/* Mapping-private opaque data:
					 	 * usually used for buffer_heads
						 * if PagePrivate set; used for
						 * swp_entry_t if PageSwapCache;
						 * indicates order in the buddy
						 * system if PG_buddy is set.
						 */
		struct address_space *mapping;	/* If low bit clear, points to
						 * inode address_space, or NULL.
						 * If page mapped as anonymous
						 * memory, low bit is set, and
						 * it points to anon_vma object:
						 * see PAGE_MAPPING_ANON below.
						 */
	    };
#if NR_CPUS >= CONFIG_SPLIT_PTLOCK_CPUS
	    spinlock_t ptl;
#endif
	};
	pgoff_t index;			/* Our offset within mapping. */
	struct list_head lru;		/* Pageout list, eg. active_list
					 * protected by zone->lru_lock !
					 */
	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(WANT_PAGE_VIRTUAL)
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */
};

#endif /* _LINUX_MM_TYPES_H */
