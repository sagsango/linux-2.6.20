#

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
