/*
 * Linux 2.6.20 — mm/shmem_acl.c
 *
 * IDE NOTES (single file, code-style)
 *
 * Focus:
 *  - Background on tmpfs/shmem ACL support
 *  - Why this file exists
 *  - How POSIX ACLs are stored in shmem inodes
 *  - xattr plumbing
 *  - permission check flow
 *  - inode lifetime / ACL lifetime
 *
 * Source basis: user-provided mm/shmem_acl.c
 */


/***************************************************************
 * 0. BIG PICTURE
 ***************************************************************

/*
This file adds POSIX ACL support to shmem/tmpfs.

shmem/tmpfs is memory-backed filesystem code.

Question this file answers:

    "How does tmpfs remember and check ACLs?"

This file is NOT about:
    - page cache / swap / reclaim internals
    - generic VFS inode permission logic itself
    - full xattr framework internals

This file IS about:
    - storing ACL pointers inside shmem inode-private data
    - exposing them through xattr handlers
    - hooking ACL checks into permission path
*/


/***************************************************************
 * 1. BACKGROUND: WHAT IS A POSIX ACL?
 ***************************************************************

/*
Traditional Unix permissions:

    user / group / other
    rwx bits only

POSIX ACLs extend that model so filesystem objects can have richer
permissions, such as:

    - named user entries
    - named group entries
    - mask entry
    - default ACL on directories

Two main ACL kinds appear here:

    ACL_TYPE_ACCESS
        -> actual access permissions for inode

    ACL_TYPE_DEFAULT
        -> default ACL inherited by new children of a directory
*/


/***************************************************************
 * 2. WHY shmem NEEDS THIS FILE
 ***************************************************************

/*
Unlike disk filesystems that may persist ACLs in on-disk metadata,
shmem/tmpfs is memory-backed.

So shmem needs a simple in-memory way to:

    - attach ACLs to inode
    - answer xattr queries like system.posix_acl_access
    - initialize inherited ACLs for new files/dirs
    - free ACLs when inode dies
    - consult ACLs during permission checks

That is exactly what this file does.
*/


/***************************************************************
 * 3. WHERE ACLS LIVE
 ***************************************************************

/*
Stored in shmem-specific inode info:

    SHMEM_I(inode)->i_acl
    SHMEM_I(inode)->i_default_acl

Interpretation:

    i_acl
        -> access ACL

    i_default_acl
        -> default ACL (usually meaningful on dirs)

These are in-memory pointers to struct posix_acl objects.
*/


/***************************************************************
 * 4. CORE ABSTRACTION: generic_acl_operations
 ***************************************************************

/*
This file builds a small adapter around generic ACL helper code.

The VFS/generic ACL layer wants callbacks like:

    getacl(inode, type)
    setacl(inode, type, acl)

So this file provides:

    shmem_get_acl()
    shmem_set_acl()

and then registers them in:

    struct generic_acl_operations shmem_acl_ops
*/


/***************************************************************
 * 5. shmem_get_acl()
 ***************************************************************

/*
Function:
    static struct posix_acl *shmem_get_acl(struct inode *inode, int type)

Purpose:
    return a DUPLICATED ACL pointer for requested type
*/


/*
Flow:

    spin_lock(&inode->i_lock)
    switch(type):
        ACCESS  -> dup SHMEM_I(inode)->i_acl
        DEFAULT -> dup SHMEM_I(inode)->i_default_acl
    spin_unlock(&inode->i_lock)
    return acl
*/


/*
Important point:

    It returns posix_acl_dup(...), not raw stored pointer.

Why?
    Because caller gets its own referenced copy/object handle semantics,
    and the internal stored ACL is protected from lifetime races.
*/


/***************************************************************
 * 6. shmem_set_acl()
 ***************************************************************

/*
Function:
    static void shmem_set_acl(struct inode *inode, int type, struct posix_acl *acl)

Purpose:
    replace stored ACL of requested type
*/


/*
Flow:

    free = NULL

    spin_lock(&inode->i_lock)
    switch(type):
        ACCESS:
            free = old i_acl
            i_acl = posix_acl_dup(acl)
        DEFAULT:
            free = old i_default_acl
            i_default_acl = posix_acl_dup(acl)
    spin_unlock(&inode->i_lock)

    posix_acl_release(free)
*/


/*
Important subtlety:

1. Replace pointer under inode->i_lock
2. Release old ACL AFTER unlocking

Why this pattern?
    - lock protects pointer swap
    - expensive release/free path should not happen while holding spinlock
*/


/***************************************************************
 * 7. shmem_acl_ops
 ***************************************************************

struct generic_acl_operations shmem_acl_ops = {
    .getacl = shmem_get_acl,
    .setacl = shmem_set_acl,
};

/*
Meaning:
    generic ACL helpers can now operate on shmem inodes through this adapter.
*/


/***************************************************************
 * 8. xattr PLUMBING: WHY IT EXISTS
 ***************************************************************

/*
User space often sees POSIX ACLs through extended attributes:

    system.posix_acl_access
    system.posix_acl_default

This file provides xattr handlers so those names map to generic ACL helpers.

So this file is the bridge:

    xattr namespace  <->  generic ACL helpers  <->  shmem inode ACL pointers
*/


/***************************************************************
 * 9. ACCESS ACL xattr HANDLER PATH
 ***************************************************************

/*
Relevant functions:

    shmem_list_acl_access()
    shmem_get_acl_access()
    shmem_set_acl_access()

Handler object:

    shmem_xattr_acl_access_handler
*/


/*
shmem_list_acl_access():
    generic_acl_list(inode, &shmem_acl_ops, ACL_TYPE_ACCESS, ...)

Meaning:
    when listing xattrs, advertise access ACL entry if appropriate.
*/


/*
shmem_get_acl_access():
    verify name == ""
    generic_acl_get(... ACL_TYPE_ACCESS ...)

Why name == "" ?
    xattr handler already matches the prefix:

        POSIX_ACL_XATTR_ACCESS

    so the remainder after prefix must be empty.
*/


/*
shmem_set_acl_access():
    verify name == ""
    generic_acl_set(... ACL_TYPE_ACCESS ...)
*/


/*
Handler registration:

struct xattr_handler shmem_xattr_acl_access_handler = {
    .prefix = POSIX_ACL_XATTR_ACCESS,
    .list   = shmem_list_acl_access,
    .get    = shmem_get_acl_access,
    .set    = shmem_set_acl_access,
};
*/


/***************************************************************
 * 10. DEFAULT ACL xattr HANDLER PATH
 ***************************************************************

/*
Exact same pattern, but for:

    ACL_TYPE_DEFAULT
    POSIX_ACL_XATTR_DEFAULT

Functions:
    shmem_list_acl_default()
    shmem_get_acl_default()
    shmem_set_acl_default()

Handler:
    shmem_xattr_acl_default_handler
*/


/***************************************************************
 * 11. ACL INITIALIZATION FOR NEW INODES
 ***************************************************************

/*
Function:
    shmem_acl_init(struct inode *inode, struct inode *dir)

Implementation:
    return generic_acl_init(inode, dir, &shmem_acl_ops);

Purpose:
    initialize ACL(s) on newly created inode, usually based on parent dir
    default ACL and mode rules.
*/


/*
This matters when creating new files/dirs in a directory which has a
DEFAULT ACL.

The generic helper handles inheritance policy; this file merely provides
shmem-specific get/set callbacks.
*/


/***************************************************************
 * 12. ACL DESTRUCTION DURING INODE LIFETIME END
 ***************************************************************

/*
Function:
    shmem_acl_destroy_inode(struct inode *inode)

Purpose:
    release in-memory ACL objects before inode is destroyed
*/


/*
Flow:

    if i_acl:
        posix_acl_release(i_acl)
    i_acl = NULL

    if i_default_acl:
        posix_acl_release(i_default_acl)
    i_default_acl = NULL
*/


/*
Meaning:
    shmem inode owns references to these ACL objects.
    inode teardown must drop them.
*/


/***************************************************************
 * 13. PERMISSION CHECK FLOW
 ***************************************************************

/*
This is the runtime access check path.

Functions:

    shmem_check_acl()
    shmem_permission()
*/


/***************************************************************
 * 14. shmem_check_acl()
 ***************************************************************

/*
Function:
    static int shmem_check_acl(struct inode *inode, int mask)

Purpose:
    callback used by generic_permission() to consult POSIX ACLs
*/


/*
Flow:

    acl = shmem_get_acl(inode, ACL_TYPE_ACCESS)

    if (acl) {
        error = posix_acl_permission(inode, acl, mask)
        posix_acl_release(acl)
        return error
    }

    return -EAGAIN
*/


/*
Important meaning of -EAGAIN here:

    "No ACL-based final answer from this callback; fall back to normal
     Unix mode-bit permission checking in generic_permission()."

So -EAGAIN is NOT a user-visible retry request here.
It is part of internal permission-decision protocol.
*/


/***************************************************************
 * 15. shmem_permission()
 ***************************************************************

/*
Function:
    int shmem_permission(struct inode *inode, int mask, struct nameidata *nd)

Implementation:
    return generic_permission(inode, mask, shmem_check_acl);

Meaning:
    shmem delegates standard VFS permission logic to generic_permission(),
    while supplying ACL callback for richer ACL-aware checks.
*/


/*
So the effective flow is:

    permission request
        -> shmem_permission()
            -> generic_permission(..., shmem_check_acl)
                -> try ACL path
                -> if no ACL answer, fallback to mode bits/capabilities logic
*/


/***************************************************************
 * 16. LOCKING MODEL
 ***************************************************************

/*
Primary lock used here:

    inode->i_lock

Used to protect:
    - SHMEM_I(inode)->i_acl
    - SHMEM_I(inode)->i_default_acl

Why spin_lock?
    These are small pointer updates/reads in inode state.
*/


/*
Pattern summary:

GET path:
    lock
    duplicate pointer target
    unlock

SET path:
    lock
    swap pointer
    unlock
    release old object
*/


/***************************************************************
 * 17. WHY DUPLICATE INSTEAD OF RETURNING STORED POINTER?
 ***************************************************************

/*
Because callers may need a stable referenced ACL object independent of
future inode updates.

If raw pointer were returned directly:
    - concurrent set/destroy could invalidate it
    - lifetime management becomes fragile

Dup/release gives clean ownership boundaries.
*/


/***************************************************************
 * 18. HOW THIS FITS INTO THE STACK
 ***************************************************************

/*
User space syscall layer:
    setxattr/getxattr/listxattr
    mkdir/create/chmod/access/open/etc.

VFS:
    xattr dispatch / generic_permission / generic ACL helpers

shmem_acl.c:
    tiny adapter for shmem-specific ACL storage

shmem inode state:
    SHMEM_I(inode)->i_acl / i_default_acl
*/


/***************************************************************
 * 19. WHAT THIS FILE DOES NOT DO
 ***************************************************************

/*
It does NOT:
    - parse ACL format itself in detail
    - implement generic ACL inheritance rules itself
    - decide full VFS permission policy itself
    - persist ACLs to disk (tmpfs/shmem is memory-backed)

Those responsibilities are delegated to:
    - generic_acl_get/set/init/list
    - generic_permission
    - posix_acl_permission
*/


/***************************************************************
 * 20. END-TO-END STORIES
 ***************************************************************

/*
A) SETTING ACCESS ACL

user sets xattr system.posix_acl_access
    -> xattr layer chooses shmem_xattr_acl_access_handler
    -> shmem_set_acl_access()
    -> generic_acl_set(... ACL_TYPE_ACCESS ...)
    -> shmem_set_acl()
    -> store duplicated ACL in SHMEM_I(inode)->i_acl
*/


/*
B) GETTING DEFAULT ACL

user gets xattr system.posix_acl_default
    -> xattr layer chooses shmem_xattr_acl_default_handler
    -> shmem_get_acl_default()
    -> generic_acl_get(... ACL_TYPE_DEFAULT ...)
    -> shmem_get_acl()
    -> duplicate and return SHMEM_I(inode)->i_default_acl
*/


/*
C) CREATING NEW FILE IN DIRECTORY WITH DEFAULT ACL

new inode created under directory
    -> shmem_acl_init(new_inode, parent_dir)
    -> generic_acl_init(...)
    -> inherited/default ACL rules applied
    -> shmem_set_acl() stores resulting ACL(s)
*/


/*
D) ACCESS CHECK

process tries operation on inode
    -> shmem_permission()
    -> generic_permission()
    -> shmem_check_acl()
    -> if access ACL exists:
           posix_acl_permission(...)
       else:
           return -EAGAIN and fallback to mode bits
*/


/***************************************************************
 * 21. INTERVIEW MENTAL MODEL
 ***************************************************************

/*
If asked:

"What is mm/shmem_acl.c doing?"

Good answer:

    It is a small adapter layer that adds POSIX ACL support to tmpfs/shmem.
    It stores access/default ACLs in shmem inode-private fields, exposes them
    via xattr handlers, initializes inherited ACLs on new inodes, frees ACLs
    at inode teardown, and hooks ACL-aware checks into generic_permission().
*/


/***************************************************************
 * 22. ONE-LINE SUMMARY
 ***************************************************************

/*
mm/shmem_acl.c is the shmem/tmpfs glue layer between VFS generic ACL/xattr
helpers and the in-memory ACL pointers stored inside shmem inodes.
*/


/***************************************************************
 * END
 ***************************************************************/

