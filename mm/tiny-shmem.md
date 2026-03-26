/*
 * Linux 2.6.20 — mm/tiny-shmem.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - VERY DETAILED background (why tiny-shmem exists)
 *  - Relationship to full shmem.c and ramfs
 *  - tmpfs-on-ramfs design
 *  - shmem_file_setup() flow
 *  - shmem_zero_setup() flow
 *  - What features are intentionally missing
 *  - How this behaves on MMU vs NOMMU systems
 *
 * Source: user-provided tiny-shmem.c
 */


/***************************************************************
 * 0. BIG PICTURE (CRITICAL)
 ***************************************************************/

/*
This file implements a VERY SMALL substitute for the full shmem/tmpfs code.

The comment at the top explains the intent clearly:

    simple shmemfs and tmpfs using ramfs code

So this file is not "full tmpfs" like mm/shmem.c.
Instead, it is a lightweight compatibility layer that provides the parts
of shmem/tmpfs that many small systems need, while deliberately avoiding
all the heavy machinery of full shmem.

The central design idea is:

    reuse ramfs as the backing implementation

rather than implementing a dedicated swap-backed tmpfs subsystem.
*/


/*
So the conceptual layering is:

    POSIX shm / anonymous shared mappings / tmpfs-style files
            ↓
        tiny-shmem glue layer
            ↓
              ramfs
            ↓
            page cache / RAM

There is NO real swap-backed tmpfs logic here.
That is the biggest thing to remember.
*/


/***************************************************************
 * 1. WHY tiny-shmem EXISTS
 ***************************************************************/

/*
The full mm/shmem.c implementation provides:

    - swap-backed tmpfs
    - resource limits
    - swap-vector management per inode
    - truncate/swap interaction
    - shmem-specific writepage and swapin/swapout logic
    - more complete tmpfs semantics

But that code is complex.

On small systems, especially systems without swap, much of that machinery
is unnecessary overhead.

So tiny-shmem exists to say:

    "if we only need simple tmpfs-like behavior in RAM,
     and we already have ramfs, let's reuse that"
*/


/*
The source comment makes the tradeoff explicit:

    benefits of full shmem code are outweighed by complexity

and:

    on systems without swap this should be effectively equivalent,
    but much lighter weight

That is the main philosophy of this file.
*/


/***************************************************************
 * 2. WHAT THIS FILE IS REALLY DOING
 ***************************************************************/

/*
This file provides three broad things:

1. Register a filesystem named "tmpfs"
   but backed by ramfs superblock/inode logic

2. Provide shmem_file_setup()
   to create an internal, unlinked tmpfs-backed file object

3. Provide shmem_zero_setup()
   to implement shared anonymous mappings via such a file

So this file is mostly integration glue, not a full memory manager.
*/


/***************************************************************
 * 3. tmpfs_fs_type (VERY IMPORTANT)
 ***************************************************************/

/*
static struct file_system_type tmpfs_fs_type = {
    .name    = "tmpfs",
    .get_sb  = ramfs_get_sb,
    .kill_sb = kill_litter_super,
};

This is the key trick.

The filesystem is called:
    "tmpfs"

But its superblock creation path is:
    ramfs_get_sb

Meaning:
    tiny-shmem exports a tmpfs-like name and interface,
    but implementation is really ramfs-backed.
*/


/*
This is why tiny-shmem is so small.
It delegates most actual filesystem behavior to ramfs.
*/


/***************************************************************
 * 4. shm_mnt (KERNEL-INTERNAL MOUNT)
 ***************************************************************/

/*
static struct vfsmount *shm_mnt;

This is a kernel-internal mount of the tmpfs_fs_type.

Why do we need it?

Because helper functions like shmem_file_setup() need a mounted filesystem
context in which they can create dentries and inodes.

So the kernel keeps one internal mount alive and uses it as the home for
anonymous/shared memory files.
*/


/***************************************************************
 * 5. init_tmpfs() — INITIALIZATION
 ***************************************************************/

/*
Function:

    static int __init init_tmpfs(void)

Flow:

1. register_filesystem(&tmpfs_fs_type)
2. kern_mount(&tmpfs_fs_type)
3. store resulting mount in shm_mnt

If either step fails, BUG_ON triggers.

Meaning:
    tiny-shmem expects tmpfs support to be available once initialized.
*/


/*
Important detail:

    kern_mount()

creates a mount for internal kernel use.
This is not primarily about a user manually mounting tmpfs.
It is about giving the kernel a place to create backing files for
shared anonymous memory objects.
*/


/***************************************************************
 * 6. shmem_file_setup() — WHAT PROBLEM IT SOLVES
 ***************************************************************/

/*
Function:

    struct file *shmem_file_setup(char *name, loff_t size, unsigned long flags)

This is one of the most important helper functions.

Purpose:

    create an unlinked file living in tmpfs/ramfs

Why?

Because many shared memory features in Linux are conveniently modeled as
file-backed VM objects, even when no user-visible pathname should exist.

So this function creates a file object that:
    - exists in memory
    - has an inode and mapping
    - has no persistent directory link
    - can back VMAs and page cache
*/


/***************************************************************
 * 7. shmem_file_setup() — DETAILED FLOW
 ***************************************************************/

/*
Step 1:
    if (IS_ERR(shm_mnt))
        return error

Meaning:
    must have internal tmpfs mount ready
*/


/*
Step 2:
    build qstr from provided name

This name is mainly for visibility/debugging, for example in places like:

    /proc/<pid>/maps

It is not about creating a normal linked filesystem pathname.
*/


/*
Step 3:
    root = shm_mnt->mnt_root
    dentry = d_alloc(root, &this)

Meaning:
    allocate a dentry under the internal mount root
*/


/*
Step 4:
    file = get_empty_filp()

Allocate a struct file object.
*/


/*
Step 5:
    inode = ramfs_get_inode(root->d_sb, S_IFREG | S_IRWXUGO, 0)

This is the core delegation to ramfs.
We allocate a regular-file inode from ramfs.
*/


/*
Step 6:
    d_instantiate(dentry, inode)
    inode->i_nlink = 0

Important meaning:

The dentry gets associated with the inode, but link count is forced to zero.
So the file is effectively UNLINKED / anonymous from normal filesystem view.

This is a classic kernel trick:
    use file/inode machinery without a user-visible persistent directory entry.
*/


/*
Step 7:
    fill file fields:
        f_path.mnt
        f_path.dentry
        f_mapping = inode->i_mapping
        f_op = &ramfs_file_operations
        f_mode = FMODE_WRITE | FMODE_READ

Meaning:
    construct a live in-kernel file object backed by ramfs mapping.
*/


/*
Step 8:
    do_truncate(dentry, size, 0, file)

This sets the file to requested size and notifies relevant layers of
size change.

This matters because VM mappings and page cache logic need file size to be
consistent with intended shared object size.
*/


/*
If all succeeds:
    return struct file *

On failure:
    unwind via put_filp / dput / error pointer
*/


/***************************************************************
 * 8. WHY inode->i_nlink = 0 IS IMPORTANT
 ***************************************************************/

/*
This makes the file anonymous/unlinked.

It still exists because active references remain:
    - the dentry
    - the struct file
    - VM mappings that may reference it

But from a namespace perspective, it is not a normal linked file.

This is ideal for internal shared memory backing objects.
*/


/***************************************************************
 * 9. shmem_zero_setup() — WHAT IT DOES
 ***************************************************************/

/*
Function:

    int shmem_zero_setup(struct vm_area_struct *vma)

Purpose:

    set up a shared anonymous mapping

This is the helper used for things like MAP_SHARED anonymous memory
style handling, traditionally associated with /dev/zero or shmem-backed
anonymous shared regions.
*/


/*
Key idea:

Instead of keeping the mapping purely anonymous,
create a hidden tmpfs-backed file and attach it to the VMA.

Why?

Because shared mappings are easier to represent and manage when multiple
VMAs can refer to a common file mapping / page cache object.
*/


/***************************************************************
 * 10. shmem_zero_setup() — DETAILED FLOW
 ***************************************************************/

/*
Flow:

1. size = vma->vm_end - vma->vm_start
2. file = shmem_file_setup("dev/zero", size, vma->vm_flags)
3. if old vm_file exists:
       fput(vma->vm_file)
4. vma->vm_file = file
5. vma->vm_ops = &generic_file_vm_ops
6. return 0

Meaning:
    convert the VMA into a file-backed shared mapping using an internal
    tmpfs/ramfs file named "dev/zero".
*/


/*
Important conceptual point:

This is how the kernel can implement shared anonymous memory with ordinary
file-backed VM machinery.

So anonymous shared memory is often not truly "nameless" internally.
Instead it becomes:

    hidden file-backed shared object
*/


/***************************************************************
 * 11. WHY generic_file_vm_ops?
 ***************************************************************/

/*
Once the VMA is backed by a real file mapping, the kernel can reuse generic
file-backed VMA operations.

This is another major simplification.
Instead of inventing custom VM operations for tiny-shmem, the code leverages
existing generic file VM behavior.
*/


/***************************************************************
 * 12. shmem_unuse() — STUB
 ***************************************************************/

/*
Function:

    int shmem_unuse(swp_entry_t entry, struct page *page)
    {
        return 0;
    }

This is a huge clue about the intended scope of tiny-shmem.

In full shmem.c, shmem_unuse() participates in swapoff / swap-cache swizzling
logic for tmpfs pages.

Here it is a no-op.

Meaning:
    tiny-shmem does not implement the full swap-backed shmem machinery.
*/


/*
This reinforces the top-level design statement:

    tiny-shmem is for small/no-swap systems

So a lot of swap-specific complexity is intentionally absent.
*/


/***************************************************************
 * 13. shmem_mmap()
 ***************************************************************/

/*
Function:

    int shmem_mmap(struct file *file, struct vm_area_struct *vma)

Flow:

1. file_accessed(file)
2. on NOMMU:
       return ramfs_nommu_mmap(file, vma)
3. on MMU:
       return 0

Meaning:
    this is a very thin wrapper.
*/


/*
Why is MMU case so trivial?

Because once file backing and generic VM operations are in place,
there is little custom work needed here.
*/


/***************************************************************
 * 14. NOMMU SUPPORT
 ***************************************************************/

/*
The file has NOMMU-specific support:

    shmem_mmap() → ramfs_nommu_mmap()
    shmem_get_unmapped_area() → ramfs_nommu_get_unmapped_area()

This matters because NOMMU systems cannot rely on the full virtual-memory
machinery available on MMU systems.

tiny-shmem is especially relevant for small embedded systems,
which often correlate with NOMMU or constrained MMU environments.
*/


/***************************************************************
 * 15. WHAT THIS FILE DELIBERATELY DOES NOT PROVIDE
 ***************************************************************/

/*
Compared to full mm/shmem.c, tiny-shmem intentionally lacks:

    - swap-backed tmpfs data storage
    - per-inode swap vector management
    - shmem_writepage swap-out path
    - shmem_getpage swap-in/out complexity
    - resource limits for blocks/inodes
    - tmpfs-specific accounting machinery
    - truncate/swap race handling
    - complex NUMA/shared policy logic
    - ACL/xattr/shmem-specific inode richness

In other words:

    this is minimal shmem compatibility, not full tmpfs behavior.
*/


/***************************************************************
 * 16. WHY ramfs IS A GOOD FIT HERE
 ***************************************************************/

/*
ramfs already gives:

    - simple in-memory filesystem semantics
    - inode/dentry/file infrastructure
    - page-cache-backed storage in RAM
    - regular file operations

For a no-swap or low-complexity system, that is "good enough" to emulate
much of tmpfs/shmem behavior.

So tiny-shmem basically says:

    "reuse ramfs and stop there"
*/


/***************************************************************
 * 17. IMPORTANT DIFFERENCE: tiny-shmem vs full shmem
 ***************************************************************/

/*
FULL SHMEM:
    RAM <-> SWAP
    tmpfs objects can be evicted to swap
    has dedicated shmem VM/file/swap logic

TINY SHMEM:
    RAM only (effectively, via ramfs semantics)
    no real shmem swap backend
    far smaller implementation

That is the most important comparison to remember.
*/


/***************************************************************
 * 18. END-TO-END FLOW: INTERNAL SHMEM FILE CREATION
 ***************************************************************/

/*
Caller wants internal shared-memory file
    ↓
shmem_file_setup(name, size, flags)
    ↓
use kernel tmpfs mount (shm_mnt)
    ↓
allocate dentry
    ↓
allocate struct file
    ↓
allocate ramfs inode
    ↓
d_instantiate(dentry, inode)
    ↓
set inode->i_nlink = 0
    ↓
fill file object
    ↓
truncate to requested size
    ↓
return file
*/


/***************************************************************
 * 19. END-TO-END FLOW: SHARED ANON MAPPING SETUP
 ***************************************************************/

/*
Shared anonymous VMA needs backing
    ↓
shmem_zero_setup(vma)
    ↓
create hidden tmpfs/ramfs file named "dev/zero"
    ↓
attach file to vma->vm_file
    ↓
set vma->vm_ops = generic_file_vm_ops
    ↓
VMA now behaves like shared file-backed mapping
*/


/***************************************************************
 * 20. INTERACTION WITH PAGE CACHE
 ***************************************************************/

/*
Because ramfs inodes have an address_space mapping, the pages of these files
naturally live in the page cache.

So tiny-shmem still uses the normal file-page path in RAM.
What it does NOT provide is the extra machinery to move those pages to/from
swap the way full shmem.c does.
*/


/***************************************************************
 * 21. RELATION TO /dev/zero STYLE SEMANTICS
 ***************************************************************/

/*
The helper name "dev/zero" in shmem_zero_setup() is a strong historical clue.

Shared anonymous mappings in Unix/Linux have long been associated with
/dev/zero-style backing semantics.

Internally, tiny-shmem models that by creating a hidden in-memory file and
using generic file-backed VM operations.
*/


/***************************************************************
 * 22. WHY THIS FILE IS SO SMALL
 ***************************************************************/

/*
Because it is mostly glue.

It depends heavily on:
    - ramfs for filesystem behavior
    - generic file VM operations for mappings
    - ordinary inode/dentry/file infrastructure

So most of the heavy lifting is delegated elsewhere.

That is exactly the point of this implementation.
*/


/***************************************************************
 * 23. INTERVIEW MENTAL MODEL
 ***************************************************************/

/*
If asked:

"What does tiny-shmem.c do?"

Good answer:

    It provides a lightweight substitute for full tmpfs/shmem by building on
    top of ramfs. It registers a tmpfs-named filesystem backed by ramfs,
    creates hidden unlinked in-memory files for shared memory objects, and
    uses those files to back shared anonymous mappings via generic file VM
    operations. It intentionally omits the complex swap-backed and resource-
    managed behavior of full shmem.c.
*/


/***************************************************************
 * 24. ONE-LINE SUMMARY
 ***************************************************************/

/*
mm/tiny-shmem.c implements a lightweight tmpfs/shmem compatibility layer by
reusing ramfs to create hidden in-memory files and shared anonymous mappings,
without the full swap-backed complexity of mm/shmem.c.
*/


/***************************************************************
 * END
 ***************************************************************/
