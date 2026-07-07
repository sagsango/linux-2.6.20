/* XXX: How drivers get initied */

For **Linux 2.6.20**, the initialization of **ext2** is a great example because it shows how the **VFS**, **filesystem registration**, **block layer**, and **driver model** all work together.

The process actually has **two distinct phases**:

1. **Filesystem registration** (during kernel/module initialization)
2. **Filesystem activation** (when an ext2 partition is mounted)

These are often confused, but they are different.

---

# Overall Flow

```text
                    Kernel Boot
                         |
                         v
                ext2_init()  <-- Register ext2 with VFS
                         |
                         v
             register_filesystem(&ext2_fs_type)
                         |
                         v
            ext2 is now known to the VFS
                         |
--------------------------------------------------------
                         |
mount /dev/sda1 /mnt
                         |
                         v
sys_mount()
                         |
                         v
VFS finds ext2_fs_type
                         |
                         v
get_sb() / ext2_get_sb()
                         |
                         v
Read Superblock
                         |
                         v
Read Group Descriptors
                         |
                         v
Initialize in-memory structures
                         |
                         v
Root inode loaded
                         |
                         v
Filesystem Ready
```

---

# Phase 1 – Kernel Boot (Filesystem Registration)

If ext2 is built into the kernel (`CONFIG_EXT2_FS=y`), its initialization happens during boot.

The kernel eventually reaches:

```text
start_kernel()
    |
    v
do_basic_setup()
    |
    v
do_initcalls()
    |
    v
ext2_init()
```

The ext2 module defines:

```c
static int __init init_ext2_fs(void)
{
    return register_filesystem(&ext2_fs_type);
}

module_init(init_ext2_fs);
```

(When built-in, `module_init()` becomes an initcall.)

---

# ext2_fs_type

The important object is:

```c
static struct file_system_type ext2_fs_type = {
    .owner      = THIS_MODULE,
    .name       = "ext2",
    .get_sb     = ext2_get_sb,
    .kill_sb    = kill_block_super,
    .fs_flags   = FS_REQUIRES_DEV,
};
```

After registration:

```text
                 VFS

+------------------------------------+
| ext2_fs_type                       |
| ext3_fs_type                       |
| proc_fs_type                       |
| sysfs_fs_type                      |
| ramfs_fs_type                      |
+------------------------------------+
```

Nothing is mounted yet.

Only the filesystem is registered.

---

# register_filesystem()

Simplified:

```c
register_filesystem()

    |

Insert ext2_fs_type

into

file_systems linked list
```

Now the kernel knows

```text
Filesystem Name

ext2

↓

Call ext2_get_sb()
```

---

# Phase 2 – Mount

User:

```bash
mount -t ext2 /dev/sda1 /mnt
```

Userspace eventually performs

```text
mount()

↓

sys_mount()
```

Kernel:

```text
sys_mount()

↓

do_mount()

↓

do_kern_mount()

↓

get_fs_type("ext2")
```

The VFS searches

```text
Registered Filesystems

proc

sysfs

ramfs

ext2

↓

Found
```

---

# ext2_get_sb()

Now VFS calls

```c
ext2_get_sb(...)
```

This is the entry point of the ext2 filesystem.

Flow:

```text
ext2_get_sb()

        |

get_sb_bdev()

        |

ext2_fill_super()
```

---

# ext2_fill_super()

This is the heart of ext2 initialization.

It performs:

```text
Read superblock

Read group descriptors

Read inode bitmap

Read block bitmap

Initialize ext2_sb_info

Load root inode

Create root dentry

Return
```

---

# Reading the Superblock

Disk:

```text
+----------------+
| Boot Sector    |
+----------------+
| Superblock     |
+----------------+
| Group Desc     |
+----------------+
| Data           |
+----------------+
```

Kernel:

```text
ext2_fill_super()

        |

sb_bread()

        |

Buffer Cache

        |

Block Device

        |

IDE Driver

        |

Disk
```

---

# Create super_block

The generic VFS object:

```c
struct super_block
```

gets filled.

Then ext2 attaches private data:

```c
sb->s_fs_info

↓

struct ext2_sb_info
```

ASCII:

```text
VFS

super_block

      |

s_fs_info

      |

ext2_sb_info
```

---

# Load Root Inode

Next:

```text
ext2_iget(ROOT_INO)
```

reads inode number 2.

Disk:

```text
inode 2

↓

Root Directory
```

Kernel:

```text
struct inode

↓

struct dentry

↓

sb->s_root
```

Now the VFS has

```text
Root

/

↓

Ready
```

---

# Filesystem Ready

Now

```text
open()

↓

lookup()

↓

ext2_lookup()

↓

inode

↓

read()

↓

ext2_readpage()
```

Everything works.

---

# Relationship with the Block Driver

Notice ext2 never talks directly to the disk controller.

Instead:

```text
ext2

↓

buffer cache

↓

generic block layer

↓

IDE/SCSI driver

↓

Hardware
```

ASCII:

```text
User

↓

VFS

↓

ext2

↓

Buffer Cache

↓

Block Layer

↓

IDE Driver

↓

SATA Controller

↓

Disk
```

---

# Source Files to Read (Linux 2.6.20)

If you're studying the source, this is the order I'd recommend:

```text
fs/ext2/
├── super.c        <-- FIRST (registration, mount)
├── inode.c        <-- inode operations
├── dir.c          <-- directory operations
├── file.c         <-- file operations
├── namei.c        <-- lookup/create/unlink
├── balloc.c       <-- block allocator
├── ialloc.c       <-- inode allocator
├── bitmap.c       <-- bitmaps
├── symlink.c
├── ioctl.c
├── xattr.c
├── acl.c
└── resize.c
```

---

# Complete Initialization Timeline

```text
Boot
 |
start_kernel()
 |
do_initcalls()
 |
ext2_init()
 |
register_filesystem()
 |
VFS knows "ext2"
 |
--------------------------------------------------
 |
mount /dev/sda1
 |
sys_mount()
 |
get_fs_type("ext2")
 |
ext2_get_sb()
 |
get_sb_bdev()
 |
ext2_fill_super()
 |
Read superblock
 |
Read group descriptors
 |
Create super_block
 |
Load root inode (#2)
 |
Create root dentry
 |
Mount successful
 |
Filesystem ready
```

---

# One important clarification

There are **two "drivers" involved**, and they initialize at different times:

1. **The block device driver** (e.g., IDE, SCSI, SATA) is initialized during hardware discovery. It creates block devices such as `/dev/sda1`.
2. **The ext2 filesystem** is initialized by registering itself with the VFS during boot (or when its module is loaded). It doesn't access hardware directly. Only when a mount occurs does the VFS call `ext2_get_sb()` and `ext2_fill_super()`, which then use the block layer to read the filesystem metadata from the block device.

Keeping these two initialization paths separate is essential for understanding how Linux layers the storage stack.

