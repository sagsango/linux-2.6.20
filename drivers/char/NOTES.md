#
========================================================================
                       QUICK NOTES: TTY DRIVERS
========================================================================

1. WHAT IS IT?
--------------
* A kernel-level component in operating systems like Linux.
* Acts as the interface between terminal devices and software applications.
* Manages text input/output, data buffering, and hardware flow control.

2. THE THREE-LAYER ARCHITECTURE
--------------------------------
* User Space:     Applications or shells (e.g., bash) reading/writing text.
* Line Discipline: Processes characters (handles backspaces, Ctrl+C, echoing).
* TTY Driver:      Communicates directly with the physical or virtual hardware.

3. TYPES OF TTY DEVICES
-----------------------
* Serial Ports:     Physical hardware interfaces (e.g., UART, /dev/ttyS0).
* Virtual Consoles: Native full-screen text terminals (e.g., /dev/tty1).
* Pseudo-TTYs:     Virtual pairs (PTYs) used by GUI terminal emulators and 
                    remote network sessions like SSH (/dev/pts/0).

========================================================================


# high level flow
                 +--------------------------------------+

                 |     User Space Application           |
                 |  (e.g., bash, cat, custom C app)     |
                 +-------------------+------------------+
                                     |
                                     | Standard I/O (read/write)
                                     v
                 +--------------------------------------+

                 |          VFS (Virtual File System)   |
                 |      Processes /dev/ttyX requests    |
                 +-------------------+------------------+
                                     |
                                     | cdev_get() / f_ops
                                     v
+========================================================================+

| TTY CORE LAYER (drivers/char/tty_io.c)                                 |
|                                                                        |
|  +------------------------------------------------------------------+  |
|  | tty_open() / tty_read() / tty_write()                            |  |
|  | - Directs VFS operations to the designated tty_struct            |  |
|  +--------------------------------+---------------------------------+  |
|                                   |                                    |
+===================================|====================================+
                                    |
                                    | Passes data stream
                                    v
+========================================================================+

| TTY LINE DISCIPLINE LAYER (drivers/char/n_tty.c)                       |
|                                                                        |
|  +------------------------------------------------------------------+  |
|  | N_TTY Line Discipline (Default)                                  |  |
|  | - Handles cooked/raw mode processing                             |  |
|  | - Buffers data using line flip buffers (tty_flip_buffer_push)    |  |
|  +--------------------------------+---------------------------------+  |
|                                   |                                    |
+===================================|====================================+
                                    |
                   +----------------+----------------+

                   | High-level TTY Operations       |
                   | (tty_driver->ops)               |
                   v                                 v
+======================================+ +===============================+

| CONSOLE/VT LAYER                    | | KEYBOARD DRIVER LAYER         |
| (drivers/char/vt.c)                  | | (drivers/char/keyboard.c)     |
|                                      | |                               |
|  +--------------------------------+  | |  +-------------------------+  |
|  | Virtual Terminal (VT) Engine   |  | |  | kbd_event()             |  |
|  | - Manages screen states (vc_pos)| | |  | - Processes raw scancodes| |
|  | - Coordinates foreground display| | |  | - Map to keycodes/ASCII |  |
|  +---------------+----------------+  | |  +------------+------------+  |
|                  |                   | |               ^               |
+==================|===================+ +===============|===============+

                   |                                     |
                   | tty->ops->write                     | Input Events
                   v                                     | (Scancodes)
+======================================+                 |

| HARDWARE SPECIFIC DRIVER             |                 |
| (e.g., drivers/char/serial_core.c)   |                 |
|                                      |                 |
|  +--------------------------------+  |                 |
|  | Low-Level Serial/UART / 8250   |  |                 |
|  | - Interacts with physical chip |  |                 |
|  +---------------+----------------+  |                 |
|                  |                   |                 |
+==================|===================+                 |

                   |                                     |
                   | Raw Hardware I/O                    |
                   v                                     |
         +-------------------+                           |

         |  Physical Devices | --------------------------+
         | (Display/UART)    |
         +-------------------+







# all source
ss@pc:~/linux-2.6.20$ find * | grep -i tty
arch/um/os-Linux/tty.c
arch/um/os-Linux/tty_log.c
arch/um/drivers/tty.c
arch/ppc/boot/simple/mpc52xx_tty.c
arch/ppc/boot/simple/m8xx_tty.c
arch/ppc/boot/simple/m8260_tty.c
arch/ppc/boot/simple/mv64x60_tty.c
Documentation/tty.txt
drivers/net/irda/irtty-sir.c
drivers/net/irda/irtty-sir.h
drivers/net/ppp_synctty.c
drivers/net/wan/pc300_tty.c
drivers/serial/jsm/jsm_tty.c
drivers/s390/char/sclp_tty.h
drivers/s390/char/tty3270.c
drivers/s390/char/sclp_tty.c
drivers/isdn/i4l/isdn_tty.c
drivers/isdn/i4l/isdn_ttyfax.h
drivers/isdn/i4l/isdn_ttyfax.c
drivers/isdn/i4l/isdn_tty.h

drivers/char/n_tty.c
drivers/char/tty_ioctl.c
drivers/char/rio/riotty.c
drivers/char/tty_io.c

fs/proc/proc_tty.c

include/net/irda/ircomm_tty_attach.h
include/net/irda/ircomm_tty.h
include/linux/tty_ldisc.h
include/linux/tty.h
include/linux/netfilter/xt_pkttype.h
include/linux/tty_driver.h
include/linux/tty_flip.h
include/linux/netfilter_ipv4/ipt_pkttype.h
include/linux/netfilter_bridge/ebt_pkttype.h
net/netfilter/xt_pkttype.c
net/irda/ircomm/ircomm_tty_ioctl.c
net/irda/ircomm/ircomm_tty_attach.c
net/irda/ircomm/ircomm_tty.c
net/bluetooth/rfcomm/tty.c
net/bridge/netfilter/ebt_pkttype.c
