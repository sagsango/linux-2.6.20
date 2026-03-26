/*
 * Linux 2.6.20 — lib/kmalloc helpers (__kzalloc, kstrdup, kmemdup, strndup_user)
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (kmalloc layer & helpers)
 *  - Why these helpers exist over raw kmalloc
 *  - Zeroing, duplication, and user→kernel copy safety
 *  - GFP flags and context constraints
 *  - Error handling patterns (ERR_PTR)
 *  - Relation to slab/slob allocators and uaccess
 *
 * Source: user-provided snippet
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
These are SMALL WRAPPER HELPERS on top of kmalloc().

They provide common patterns:

    - allocate + zero (__kzalloc)
    - allocate + duplicate string (kstrdup)
    - allocate + duplicate memory (kmemdup)
    - allocate + copy from USER safely (strndup_user)

Layering:

    helpers (this file)
        ↓
    kmalloc()/kfree()
        ↓
    slab/slob allocator (mm/slab.c or mm/slob.c)
        ↓
    buddy allocator (page allocator)

So these are convenience + safety APIs, not new allocators.
*/


/***************************************************************
 * 1. WHY NOT USE kmalloc() DIRECTLY?
 ***************************************************************/

/*
Raw kmalloc() gives you uninitialized memory and no copying.

Common bugs without helpers:

    - forgetting to zero memory (info leak / logic bugs)
    - off-by-one in string duplication (missing NUL)
    - incorrect length handling
    - unsafe user-space copies

These helpers encode the correct patterns once and reuse them.
*/


/***************************************************************
 * 2. GFP FLAGS (VERY IMPORTANT)
 ***************************************************************/

/*
All helpers take a gfp_t (except strndup_user uses GFP_KERNEL).

Examples:

    GFP_KERNEL  → can sleep (normal context)
    GFP_ATOMIC  → cannot sleep (interrupt/spinlock context)

Choosing wrong GFP flag can deadlock or fail allocations.
*/


/***************************************************************
 * 3. __kzalloc() — ALLOCATE + ZERO
 ***************************************************************/

/*
void *__kzalloc(size_t size, gfp_t flags)
*/


/*
Flow:

1. ret = kmalloc_track_caller(size, flags)
2. if (ret)
       memset(ret, 0, size)
3. return ret
*/


/*
Meaning:

    same as kmalloc() but guarantees zeroed memory

Equivalent conceptually to user-space calloc().
*/


/*
Why important in kernel?

    - avoids leaking stale kernel data
    - ensures predictable initialization
*/


/***************************************************************
 * 4. kmalloc_track_caller() (IMPORTANT DETAIL)
 ***************************************************************/

/*
Used instead of plain kmalloc().

Purpose:
    track allocation call site (debugging / profiling)

Helps with:
    - memory leak debugging
    - slab debugging tools
*/


/***************************************************************
 * 5. kstrdup() — STRING DUPLICATION
 ***************************************************************/

/*
char *kstrdup(const char *s, gfp_t gfp)
*/


/*
Flow:

1. if (!s) return NULL
2. len = strlen(s) + 1
3. buf = kmalloc_track_caller(len, gfp)
4. if (buf)
       memcpy(buf, s, len)
5. return buf
*/


/*
Important details:

    +1 ensures space for NUL terminator

So resulting string is always properly terminated.
*/


/*
Common usage:

    duplicating kernel strings (filenames, keys, paths, etc.)
*/


/***************************************************************
 * 6. kmemdup() — GENERIC MEMORY DUPLICATION
 ***************************************************************/

/*
void *kmemdup(const void *src, size_t len, gfp_t gfp)
*/


/*
Flow:

1. p = kmalloc_track_caller(len, gfp)
2. if (p)
       memcpy(p, src, len)
3. return p
*/


/*
Difference from kstrdup():

    - no NUL handling
    - works for arbitrary binary buffers
*/


/*
Use cases:

    - duplicating structs
    - copying kernel buffers
    - cloning metadata blocks
*/


/***************************************************************
 * 7. strndup_user() — USER → KERNEL SAFE COPY (CRITICAL)
 ***************************************************************/

/*
char *strndup_user(const char __user *s, long n)
*/


/*
This is the MOST IMPORTANT helper here from a security standpoint.

It copies a string from USER SPACE into kernel memory SAFELY.
*/


/***************************************************************
 * 8. USER POINTER RULE (CRITICAL)
 ***************************************************************/

/*
Kernel MUST NOT trust user pointers.

Direct dereference like:

    *s

is illegal and unsafe.

Instead kernel uses:

    copy_from_user()
    strnlen_user()
*/


/***************************************************************
 * 9. strndup_user() FLOW
 ***************************************************************/

/*
1. length = strnlen_user(s, n)

   returns:
       0      → invalid pointer
       > n    → string too long

2. if (!length)
       return ERR_PTR(-EFAULT)

3. if (length > n)
       return ERR_PTR(-EINVAL)

4. p = kmalloc(length, GFP_KERNEL)

5. if (!p)
       return ERR_PTR(-ENOMEM)

6. if (copy_from_user(p, s, length))
       kfree(p)
       return ERR_PTR(-EFAULT)

7. p[length - 1] = '\0'

8. return p
*/


/***************************************************************
 * 10. ERROR HANDLING (ERR_PTR PATTERN)
 ***************************************************************/

/*
Instead of returning NULL, kernel often returns:

    ERR_PTR(-errno)

Caller must check:

    IS_ERR(ptr)
    PTR_ERR(ptr)

This allows encoding errors in pointer values.
*/


/***************************************************************
 * 11. WHY FORCE p[length - 1] = '\0'?
 ***************************************************************/

/*
Even after copy_from_user(), kernel ensures NUL termination.

Reason:
    defensive programming against malformed user input
*/


/***************************************************************
 * 12. SECURITY INSIGHT (VERY IMPORTANT)
 ***************************************************************/

/*
strndup_user() prevents:

    - reading beyond user buffer
    - kernel crashes due to bad pointers
    - missing termination bugs

It is a canonical safe pattern for user → kernel string transfer.
*/


/***************************************************************
 * 13. RELATION TO uaccess LAYER
 ***************************************************************/

/*
Functions used:

    strnlen_user()
    copy_from_user()

These handle:

    - page faults from user memory
    - access validation
    - exception-safe copying
*/


/***************************************************************
 * 14. PERFORMANCE CONSIDERATIONS
 ***************************************************************/

/*
These helpers are lightweight, but:

    - kmalloc() may be expensive
    - copy_from_user() can fault

So avoid unnecessary duplication in hot paths.
*/


/***************************************************************
 * 15. COMMON PATTERNS
 ***************************************************************/

/*
Zeroed allocation:

    p = kzalloc(sizeof(*p), GFP_KERNEL)

Duplicate kernel string:

    p = kstrdup(name, GFP_KERNEL)

Duplicate buffer:

    p = kmemdup(src, len, GFP_KERNEL)

Copy user string:

    p = strndup_user(user_ptr, maxlen)
*/


/***************************************************************
 * 16. WHAT THESE HELPERS DO NOT DO
 ***************************************************************/

/*
They do NOT:

    - manage lifetime automatically
    - free memory (caller must kfree)
    - guarantee physical contiguity beyond kmalloc guarantees
    - handle large allocations (vmalloc needed)
*/


/***************************************************************
 * 17. END-TO-END FLOW (EXAMPLE)
 ***************************************************************/

/*
User passes string to syscall
    ↓
strndup_user()
    ↓
kmalloc()
    ↓
copy_from_user()
    ↓
kernel uses safe copy
*/


/***************************************************************
 * 18. KEY INSIGHT (VERY IMPORTANT)
 ***************************************************************/

/*
These helpers encode SAFE MEMORY USAGE PATTERNS in kernel:

    allocate → initialize → copy → validate

They reduce boilerplate and prevent subtle bugs.
*/


/***************************************************************
 * 19. INTERVIEW MODEL
 ***************************************************************/

/*
These functions are wrappers over kmalloc providing common allocation
patterns: zeroed memory (__kzalloc), string duplication (kstrdup), generic
buffer duplication (kmemdup), and safe user-space string copying
(strndup_user) using uaccess helpers.
*/


/***************************************************************
 * 20. ONE-LINE SUMMARY
 ***************************************************************/

/*
These helpers wrap kmalloc to provide safe and convenient patterns for
zeroed allocation, memory duplication, and secure user-to-kernel string
copying.
*/


/***************************************************************
 * END
 ***************************************************************/

