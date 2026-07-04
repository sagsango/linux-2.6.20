/*
============================================================
FILE: scsi_ioctl.c (Linux 2.6)
IDE STUDY NOTES (Short)
============================================================

1. PURPOSE
------------------------------------------------------------

This file implements generic SCSI command ioctl handling for block
devices.

It allows user space to send SCSI/ATAPI packet commands to a block
device through ioctls such as:

    SG_IO
    CDROM_SEND_PACKET
    SCSI_IOCTL_SEND_COMMAND

Main idea:

    user command
        |
        v
    validate permission
        |
        v
    build block request
        |
        v
    map user buffer
        |
        v
    execute request synchronously
        |
        v
    copy status/sense data back to user

This file is not a SCSI low-level driver. It is a bridge between
user-space ioctl interfaces and the block request layer.

============================================================
2. WHERE IT FITS
------------------------------------------------------------

User space
    |
ioctl(fd, SG_IO, ...)
    |
VFS / block device ioctl
    |
scsi_cmd_ioctl()
    |
sg_io()
    |
blk_get_request()
    |
blk_rq_map_user()
    |
blk_execute_rq()
    |
request_queue / driver
    |
SCSI / ATAPI device

So this file converts user-space SCSI command requests into normal
block-layer requests of type:

    REQ_TYPE_BLOCK_PC

PC means packet command.

============================================================
3. IMPORTANT COMMAND INTERFACES
------------------------------------------------------------

SG_IO
    Modern generic SCSI command interface.

CDROM_SEND_PACKET
    CD-ROM packet command wrapper converted internally to SG_IO style.

SCSI_IOCTL_SEND_COMMAND
    Old deprecated SCSI command interface.

SG_GET_VERSION_NUM
    Returns sg interface version.

SG_SET_TIMEOUT / SG_GET_TIMEOUT
    Configure command timeout.

SG_SET_RESERVED_SIZE / SG_GET_RESERVED_SIZE
    Configure reserved buffer size.

CDROMEJECT / CDROMCLOSETRAY
    Send START_STOP_UNIT style command.

============================================================
4. SECURITY MODEL: verify_command()
------------------------------------------------------------

verify_command() checks whether user space is allowed to send a SCSI
opcode.

Command classes:

    CMD_READ_SAFE
        Allowed for anyone who can open the device.

    CMD_WRITE_SAFE
        Requires the device file to be opened writable.

    unknown/dangerous commands
        Require CAP_SYS_RAWIO.

Examples of read-safe commands:

    TEST_UNIT_READY
    REQUEST_SENSE
    READ_10
    INQUIRY
    MODE_SENSE
    READ_CAPACITY
    READ_TOC

Examples of write-safe commands:

    WRITE_10
    MODE_SELECT
    FORMAT_UNIT
    FLUSH_CACHE
    PREVENT_ALLOW_MEDIUM_REMOVAL
    LOAD_UNLOAD

Purpose:

    Prevent unprivileged users from sending destructive or dangerous raw
    SCSI commands.

============================================================
5. SG_IO FLOW
------------------------------------------------------------

Function:

    sg_io(file, q, disk, hdr)

Input:

    struct sg_io_hdr

Important fields:

    interface_id
    cmdp
    cmd_len
    dxfer_direction
    dxferp
    dxfer_len
    sbp
    mx_sb_len
    timeout

Flow:

    validate interface_id == 'S'
    validate command length
    copy command from user
    verify_command()
    validate transfer length
    allocate request with blk_get_request()
    copy CDB into rq->cmd
    attach sense buffer
    set rq->cmd_type = REQ_TYPE_BLOCK_PC
    set timeout
    map user buffer into request
    execute request using blk_execute_rq()
    fill sg_io_hdr status fields
    copy sense data to user if present
    unmap user buffer
    release request

============================================================
6. REQUEST CREATED BY SG_IO
------------------------------------------------------------

The request is not a normal filesystem read/write request.

It is:

    rq->cmd_type = REQ_TYPE_BLOCK_PC

Meaning:

    a raw packet command request

Request contains:

    rq->cmd[]
        SCSI CDB

    rq->cmd_len
        command length

    rq->sense
        sense buffer

    rq->timeout
        timeout

    rq->bio
        mapped user data buffer, if any

============================================================
7. DATA TRANSFER DIRECTIONS
------------------------------------------------------------

SG_DXFER_TO_DEV
    User buffer is written to device.
    Request direction is WRITE.

SG_DXFER_FROM_DEV
    Device writes data back to user buffer.
    Request direction is READ.

SG_DXFER_TO_FROM_DEV
    Bidirectional-like handling in this older path.

SG_DXFER_NONE
    Command has no data transfer.

============================================================
8. SENSE DATA
------------------------------------------------------------

SCSI commands may fail with detailed sense data.

This file provides:

    char sense[SCSI_SENSE_BUFFERSIZE];

After command execution:

    if rq->sense_len && hdr->sbp:
        copy sense buffer to user

User space uses sense data to understand device error details.

Examples:

    medium error
    illegal request
    not ready
    unit attention

============================================================
9. LEGACY SCSI_IOCTL_SEND_COMMAND
------------------------------------------------------------

Function:

    sg_scsi_ioctl()

This is the old deprecated interface.

Limitations:

    command length inferred from opcode
    transfer limited to PAGE_SIZE
    old sense-buffer layout
    less flexible than SG_IO

Flow:

    copy input/output lengths
    copy opcode and command
    allocate temporary buffer
    allocate request
    verify command
    set timeout based on opcode
    map kernel buffer
    execute request
    copy result or sense data back

The code warns:

    "program is using a deprecated SCSI ioctl, please convert it to SG_IO"

============================================================
10. scsi_cmd_ioctl()
------------------------------------------------------------

This is the main dispatcher.

Flow:

    get request_queue from gendisk
    blk_get_queue()
    switch ioctl command:
        SG_GET_VERSION_NUM
        SCSI_IOCTL_GET_IDLUN
        SCSI_IOCTL_GET_BUS_NUMBER
        SG_SET_TIMEOUT
        SG_GET_TIMEOUT
        SG_GET_RESERVED_SIZE
        SG_SET_RESERVED_SIZE
        SG_EMULATED_HOST
        SG_IO
        CDROM_SEND_PACKET
        SCSI_IOCTL_SEND_COMMAND
        CDROMCLOSETRAY
        CDROMEJECT
    blk_put_queue()

If command is unknown:

    return -ENOTTY

============================================================
11. CDROM_SEND_PACKET
------------------------------------------------------------

CDROM_SEND_PACKET uses:

    struct cdrom_generic_command

The code converts it into an SG_IO-style header:

    cdrom_generic_command
        |
        v
    sg_io_hdr
        |
        v
    sg_io()

So CD-ROM packet commands reuse the same SG_IO execution path.

============================================================
12. BASIC START/STOP COMMANDS
------------------------------------------------------------

Helper:

    __blk_send_generic()

Builds a simple 6-byte packet command.

Used by:

    blk_send_start_stop()

Commands:

    CDROMEJECT
        START_STOP_UNIT with eject/load value

    CDROMCLOSETRAY
        START_STOP_UNIT close tray value

Flow:

    allocate request
    fill rq->cmd[0]
    fill rq->cmd[4]
    execute request
    release request

============================================================
13. COMPLETE SG_IO ASCII FLOW
------------------------------------------------------------

User program
    |
    v
ioctl(fd, SG_IO, &hdr)
    |
    v
scsi_cmd_ioctl()
    |
    v
copy sg_io_hdr from user
    |
    v
sg_io()
    |
    +--> copy CDB from user
    +--> verify_command()
    +--> blk_get_request()
    +--> rq->cmd_type = REQ_TYPE_BLOCK_PC
    +--> blk_rq_map_user()
    +--> blk_execute_rq()
    +--> collect status
    +--> copy sense data
    +--> blk_rq_unmap_user()
    +--> blk_put_request()
    |
    v
copy updated hdr to user

============================================================
14. IMPORTANT FUNCTIONS
------------------------------------------------------------

verify_command()
    Security check for SCSI opcode.

sg_io()
    Modern SG_IO request path.

sg_scsi_ioctl()
    Deprecated old SCSI command interface.

scsi_cmd_ioctl()
    Main ioctl dispatcher.

__blk_send_generic()
    Send small generic packet command.

blk_send_start_stop()
    Helper for eject/close tray operations.

sg_set_timeout()
    Set queue SCSI generic timeout.

sg_set_reserved_size()
    Set reserved SG buffer size.

============================================================
15. MENTAL MODEL
------------------------------------------------------------

This file lets user space send controlled raw SCSI/ATAPI commands through
the block layer.

It does not directly talk to hardware.

Instead:

    ioctl command
        |
        v
    block request
        |
        v
    request queue
        |
        v
    driver

Security is important because raw SCSI commands can read, write, format,
eject, lock media, or change device state.

One-line summary:

    scsi_ioctl.c implements the generic SCSI ioctl bridge for block
    devices, validating user SCSI commands, mapping user buffers, building
    REQ_TYPE_BLOCK_PC requests, executing them through the block layer, and
    returning command status and sense data to user space.
*/

