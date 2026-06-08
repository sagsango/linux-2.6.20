===============================================================================
AUDIT SYSCALL CLASSIFICATION
File: arch/x86_64/kernel/audit.c
===============================================================================

PURPOSE
=======

This file helps Linux audit classify syscalls into groups.

The audit subsystem wants to answer questions like:

    Is this syscall reading data?
    Is this syscall writing data?
    Is this syscall changing file attributes?
    Is this syscall modifying a directory?
    Is this syscall execve?
    Is this syscall open/openat?


Instead of checking every syscall one by one everywhere,
Linux groups syscalls into audit classes.


===============================================================================
BACKGROUND: WHAT IS LINUX AUDIT?
===============================================================================

Linux audit is a kernel security logging system.

It can record security-relevant events such as:

    - file opens
    - file writes
    - chmod/chown
    - execve
    - directory modification
    - permission failures
    - login/security events


Example audit idea:

    "Log every write to /etc/passwd"

or:

    "Log every execve by this user"


To do this efficiently, audit needs syscall classification.


===============================================================================
WHY CLASSIFY SYSCALLS?
===============================================================================

Suppose audit rule says:

    Watch file writes

Then audit does not want to manually compare:

    write
    writev
    pwrite64
    open with write flags
    truncate
    rename
    unlink
    mkdir
    chmod
    chown
    ...

everywhere.

Instead it uses classes:

    AUDIT_CLASS_READ
    AUDIT_CLASS_WRITE
    AUDIT_CLASS_DIR_WRITE
    AUDIT_CLASS_CHATTR


So a syscall can be quickly matched against a group.


===============================================================================
CLASS TABLES
===============================================================================

This file defines four syscall class arrays.

-------------------------------------------------------------------------------
1. Directory write class
-------------------------------------------------------------------------------

static unsigned dir_class[] = {
#include <asm-generic/audit_dir_write.h>
    ~0U
};

Meaning:

    Syscalls that modify directory structure.

Examples conceptually:

    mkdir
    rmdir
    unlink
    rename
    link
    symlink
    mknod


-------------------------------------------------------------------------------
2. Read class
-------------------------------------------------------------------------------

static unsigned read_class[] = {
#include <asm-generic/audit_read.h>
    ~0U
};

Meaning:

    Syscalls that read data.

Examples conceptually:

    read
    readv
    pread
    recvfrom


-------------------------------------------------------------------------------
3. Write class
-------------------------------------------------------------------------------

static unsigned write_class[] = {
#include <asm-generic/audit_write.h>
    ~0U
};

Meaning:

    Syscalls that write data.

Examples conceptually:

    write
    writev
    pwrite
    sendto


-------------------------------------------------------------------------------
4. Change attribute class
-------------------------------------------------------------------------------

static unsigned chattr_class[] = {
#include <asm-generic/audit_change_attr.h>
    ~0U
};

Meaning:

    Syscalls that change file metadata.

Examples conceptually:

    chmod
    chown
    utime
    setxattr
    removexattr


===============================================================================
WHY INCLUDE HEADER FILES INSIDE ARRAYS?
===============================================================================

Code:

static unsigned write_class[] = {
#include <asm-generic/audit_write.h>
    ~0U
};

The included file likely expands to syscall numbers.

Example idea:

    __NR_write,
    __NR_writev,
    __NR_pwrite64,

So after preprocessing:

static unsigned write_class[] = {
    __NR_write,
    __NR_writev,
    __NR_pwrite64,
    ~0U
};


The final value:

    ~0U

means:

    End of list marker

It is all bits set:

    0xffffffff


===============================================================================
AUDIT CLASSIFY SYSCALL
===============================================================================

Function:

    audit_classify_syscall(int abi, unsigned syscall)

Purpose:

Return special classification for important syscalls.


Code flow:

audit_classify_syscall(abi, syscall)
        |
        +--> If IA32 emulation and ABI is i386
        |       |
        |       +--> ia32_classify_syscall(syscall)
        |
        +--> Otherwise x86-64 syscall
                |
                +--> open   -> return 2
                |
                +--> openat -> return 3
                |
                +--> execve -> return 5
                |
                +--> other  -> return 0


ASCII:

             syscall
                |
                v
        +---------------+
        | ABI is i386 ? |
        +-------+-------+
                |
        +-------+--------+
        |                |
       yes              no
        |                |
        v                v
 ia32 classifier     x86_64 switch
                         |
                         +--> open   = 2
                         +--> openat = 3
                         +--> execve = 5
                         +--> else   = 0


Return values:

    0 = normal / no special classification
    2 = open
    3 = openat
    5 = execve


Why open/openat special?

Because open/openat can be read or write depending on flags.

Example:

    open("file", O_RDONLY)  -> read-like
    open("file", O_WRONLY)  -> write-like
    open("file", O_CREAT)   -> directory/file creation effect


Why execve special?

Because execve is security-important:

    process image changes
    credentials may change
    executable file is used


===============================================================================
IA32 EMULATION SUPPORT
===============================================================================

#ifdef CONFIG_IA32_EMULATION

64-bit Linux can run 32-bit userspace binaries.

But 32-bit syscall numbers are different from 64-bit syscall numbers.

Example:

    x86-64 __NR_open  !=  i386 __NR_open

So this file delegates:

    ia32_classify_syscall(syscall)


Also registers 32-bit syscall classes:

    AUDIT_CLASS_WRITE_32
    AUDIT_CLASS_READ_32
    AUDIT_CLASS_DIR_WRITE_32
    AUDIT_CLASS_CHATTR_32


===============================================================================
REGISTERING AUDIT CLASSES
===============================================================================

Function:

    audit_classes_init()

Called at boot by:

    __initcall(audit_classes_init);


Flow:

audit_classes_init()
        |
        +--> register IA32 classes if enabled
        |
        +--> register native x86-64 write class
        |
        +--> register native x86-64 read class
        |
        +--> register native x86-64 directory write class
        |
        +--> register native x86-64 change attribute class
        |
        v
Audit subsystem knows syscall groups


Code:

audit_register_class(AUDIT_CLASS_WRITE, write_class);
audit_register_class(AUDIT_CLASS_READ, read_class);
audit_register_class(AUDIT_CLASS_DIR_WRITE, dir_class);
audit_register_class(AUDIT_CLASS_CHATTR, chattr_class);


Meaning:

    Class ID                 -> syscall list

    AUDIT_CLASS_WRITE        -> write_class[]
    AUDIT_CLASS_READ         -> read_class[]
    AUDIT_CLASS_DIR_WRITE    -> dir_class[]
    AUDIT_CLASS_CHATTR       -> chattr_class[]


===============================================================================
BOOT-TIME FLOW
===============================================================================

Kernel Boot
    |
    v
audit_classes_init()
    |
    +--> register write_class
    |
    +--> register read_class
    |
    +--> register dir_class
    |
    +--> register chattr_class
    |
    v
Audit system ready


===============================================================================
RUNTIME FLOW
===============================================================================

User process makes syscall:

    write(fd, buf, len)

Flow:

User Process
    |
    v
syscall entry
    |
    v
audit subsystem
    |
    v
classify syscall
    |
    v
Is syscall in AUDIT_CLASS_WRITE?
    |
    +--> yes
            |
            v
        apply audit rules
            |
            v
        maybe log event


===============================================================================
EXAMPLE: WRITE AUDIT
===============================================================================

Audit rule:

    Watch writes to /etc/passwd

Runtime:

Process calls:

    open("/etc/passwd", O_WRONLY)

Audit sees:

    syscall = open
    audit_classify_syscall() returns 2

Then audit examines open flags:

    O_WRONLY / O_RDWR / O_CREAT / O_TRUNC

If write-like:

    generate audit event


===============================================================================
EXAMPLE: DIRECTORY WRITE
===============================================================================

Process calls:

    unlink("/tmp/a")

Flow:

unlink syscall
     |
     v
dir_class[]
     |
     v
AUDIT_CLASS_DIR_WRITE
     |
     v
audit rule match?
     |
     v
log if needed


Directory write means filesystem namespace changed.


===============================================================================
EXAMPLE: ATTRIBUTE CHANGE
===============================================================================

Process calls:

    chmod("file", 0600)

Flow:

chmod syscall
     |
     v
chattr_class[]
     |
     v
AUDIT_CLASS_CHATTR
     |
     v
audit rule match?
     |
     v
log if needed


Attribute change means metadata changed.


===============================================================================
WHY THIS FILE IS ARCH-SPECIFIC
===============================================================================

Syscall numbers are architecture-specific.

Example:

    x86-64 syscall table != i386 syscall table

Therefore each architecture needs audit classification glue.

This file is x86-64-specific.

It knows:

    __NR_open
    __NR_openat
    __NR_execve

for x86-64.


===============================================================================
COMPLETE FLOW
===============================================================================

Build Time
    |
    v
Headers define syscall numbers
    |
    v
audit.c builds syscall class arrays


Boot Time
    |
    v
audit_classes_init()
    |
    v
audit_register_class()
    |
    v
Audit class tables installed


Runtime
    |
    v
Process enters syscall
    |
    v
Audit checks syscall number
    |
    v
Class match?
    |
    +--> read class
    +--> write class
    +--> dir write class
    +--> chattr class
    +--> open/openat special
    +--> execve special
    |
    v
Audit rule matched?
    |
    +--> yes -> log audit event
    |
    +--> no  -> continue syscall


===============================================================================
KEY IDEA
===============================================================================

This file does not implement audit logging itself.

It only teaches the audit subsystem:

    "On x86-64, these syscall numbers belong to these audit classes."

It is a translation layer:

    architecture syscall numbers
              |
              v
    generic audit classes


Without this file, audit would not know how to efficiently group x86-64
syscalls into read/write/attribute/directory-write/exec/open categories.
===============================================================================
