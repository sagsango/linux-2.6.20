```text
===============================================================================
EARLY PRINTK / EARLY CONSOLE
File: arch/x86_64/kernel/early_printk.c
===============================================================================

PURPOSE
=======

This file provides very early kernel printing.

Normal printk depends on the regular console subsystem being initialized.

But during very early boot, normal consoles may not exist yet.

So Linux needs a simple emergency/debug printing path:

    early_printk()


It can print to:

    1. VGA text memory
    2. Serial port
    3. AMD SimNow simulator file


===============================================================================
WHY EARLY PRINTK EXISTS
===============================================================================

Normal boot:

    start_kernel()
        |
        v
    console_init()
        |
        v
    printk works normally


But many bugs happen before console_init().

Example:

    early page table setup fails
    e820 parsing fails
    APIC mapping fails
    ACPI setup fails
    bootmem setup fails


Without early_printk:

    Kernel dies silently


With early_printk:

    early_printk("debug message\n");

can print very early.


===============================================================================
BIG PICTURE
===============================================================================

early_printk()
      |
      v
early_console->write()
      |
      +--> VGA text buffer
      |
      +--> serial port
      |
      +--> SimNow host file


===============================================================================
EARLY VGA CONSOLE
===============================================================================

VGA text mode memory:

    physical address = 0xB8000

On x86_64 kernel direct map:

    VGABASE = 0xffffffff800b8000


Text mode layout:

Each screen character uses 2 bytes:

    byte 0 = ASCII character
    byte 1 = color attribute


Example:

    'A' with gray color

    value = 0x0741


Memory layout:

+--------------------------------------------------+
| row 0 col 0 | row 0 col 1 | row 0 col 2 | ...    |
+--------------------------------------------------+
| row 1 col 0 | row 1 col 1 | row 1 col 2 | ...    |
+--------------------------------------------------+

Address formula:

    VGABASE + 2 * (80 * y + x)


===============================================================================
early_vga_write()
===============================================================================

Function:

    early_vga_write(struct console *con, const char *str, unsigned n)

Purpose:

Write characters directly into VGA text memory.


Flow:

for each character:
    |
    +--> if screen full
    |       |
    |       +--> scroll one line up
    |
    +--> if character == '\n'
    |       |
    |       +--> xpos = 0
    |       +--> ypos++
    |
    +--> else if character != '\r'
            |
            +--> write char to VGA memory
            |
            +--> xpos++
            |
            +--> if xpos >= width
                    |
                    +--> xpos = 0
                    +--> ypos++


===============================================================================
VGA SCROLLING
===============================================================================

If:

    current_ypos >= max_ypos

then screen is full.

Code copies each row upward:

    row 1 -> row 0
    row 2 -> row 1
    row 3 -> row 2
    ...

Then clears last row.


Before:

+----------------+
| line 0         |
| line 1         |
| line 2         |
| ...            |
| line 24        |
+----------------+

After scroll:

+----------------+
| line 1         |
| line 2         |
| line 3         |
| ...            |
| blank line     |
+----------------+


===============================================================================
EARLY SERIAL CONSOLE
===============================================================================

Default serial port:

    ttyS0 = 0x3f8

Registers:

TXR
    Transmit register

LSR
    Line status register

LCR
    Line control register

IER
    Interrupt enable register

FCR
    FIFO control register

MCR
    Modem control register

DLL/DLH
    Baud rate divisor latch


===============================================================================
early_serial_putc()
===============================================================================

Purpose:

Write one character to serial port.

Flow:

early_serial_putc(ch)
      |
      +--> wait until transmitter ready
      |
      +--> outb(ch, TXR)
      |
      v
character sent


Check transmitter ready:

    inb(base + LSR) & XMTRDY

XMTRDY = 0x20


If not ready:

    cpu_relax()

until timeout.


===============================================================================
early_serial_write()
===============================================================================

Purpose:

Write string to serial port.

Flow:

for each char:
    |
    +--> early_serial_putc(char)
    |
    +--> if char == '\n'
            |
            +--> also send '\r'


Why send '\r' after '\n'?

Many serial terminals expect:

    CR + LF

for a new line.


===============================================================================
early_serial_init()
===============================================================================

Purpose:

Initialize serial port early.

Examples:

    earlyprintk=serial

    earlyprintk=serial,ttyS0,115200

    earlyprintk=ttyS1,9600

    earlyprintk=serial,0x3f8,115200


Flow:

early_serial_init()
      |
      +--> parse port
      |
      +--> configure 8N1
      |
      +--> disable interrupts
      |
      +--> disable FIFO
      |
      +--> set DTR/RTS
      |
      +--> parse baud
      |
      +--> set divisor


8N1 means:

    8 data bits
    no parity
    1 stop bit


Baud divisor:

    divisor = 115200 / baud


Example:

    baud = 9600

    divisor = 12


===============================================================================
SIMNOW CONSOLE
===============================================================================

SimNow was AMD's simulator.

This code provides output to a host file.

Instead of writing to VGA or serial:

    Linux guest
        |
        v
    CPUID magic call
        |
        v
    SimNow host file


Functions:

    simnow_init()
    simnow_write()


Magic:

    MAGIC1 = 0xBACCD00A
    MAGIC2 = 0xCA110000


This is simulator-specific debug output.


===============================================================================
CONSOLE OBJECTS
===============================================================================

Each backend is represented as struct console.

VGA:

    early_vga_console

Serial:

    early_serial_console

SimNow:

    simnow_console


Common field:

    .write = backend_write_function


So:

early_console
      |
      +--> early_vga_console.write
      |
      +--> early_serial_console.write
      |
      +--> simnow_console.write


===============================================================================
early_printk()
===============================================================================

Function:

    early_printk(const char *fmt, ...)

Purpose:

Simple printf-like debug output.

Flow:

early_printk()
      |
      +--> format message into local buffer
      |
      +--> early_console->write()
      |
      v
message printed


Code idea:

    vscnprintf(buf, 512, fmt, ap);
    early_console->write(early_console, buf, n);


Default:

    early_console = early_vga_console


===============================================================================
BOOT PARAMETER
===============================================================================

Handled by:

    setup_early_printk()

Registered by:

    early_param("earlyprintk", setup_early_printk);


Examples:

    earlyprintk=vga

    earlyprintk=serial

    earlyprintk=serial,ttyS0,115200

    earlyprintk=ttyS1,9600

    earlyprintk=simnow

    earlyprintk=vga,keep


===============================================================================
setup_early_printk()
===============================================================================

Flow:

setup_early_printk(buf)
      |
      +--> already initialized?
      |       |
      |       +--> return
      |
      +--> contains "keep"?
      |       |
      |       +--> keep_early = 1
      |
      +--> starts with "serial"?
      |       |
      |       +--> init serial
      |       +--> early_console = serial
      |
      +--> starts with "ttyS"?
      |       |
      |       +--> init serial
      |       +--> early_console = serial
      |
      +--> starts with "vga"?
      |       |
      |       +--> use VGA console
      |
      +--> starts with "simnow"?
      |       |
      |       +--> use SimNow console
      |
      +--> register_console(early_console)


===============================================================================
DISABLING EARLY PRINTK
===============================================================================

Function:

    disable_early_printk()

Called later when normal console exists.

Flow:

disable_early_printk()
      |
      +--> early console initialized?
      |
      +--> keep_early set?
              |
              +--> yes:
              |       printk("keeping early console")
              |
              +--> no:
                      printk("disabling early console")
                      unregister_console(early_console)


Why disable it?

Once normal console is ready, early console is no longer needed.

Keeping both can duplicate output.


===============================================================================
COMPLETE EARLY PRINT FLOW
===============================================================================

Kernel command line:

    earlyprintk=serial,ttyS0,115200

Boot:

start_kernel()
      |
      v
parse early params
      |
      v
setup_early_printk()
      |
      +--> early_serial_init()
      |
      +--> early_console = early_serial_console
      |
      +--> register_console()
      |
      v
early_printk("hello")
      |
      v
early_serial_write()
      |
      v
outb() to COM1 port
      |
      v
message appears on serial terminal


===============================================================================
VGA OUTPUT FLOW
===============================================================================

early_printk("Booting\n")
      |
      v
format string into buffer
      |
      v
early_vga_write()
      |
      v
writew(0x0742, VGABASE + offset)
      |
      v
visible text on screen


===============================================================================
SERIAL OUTPUT FLOW
===============================================================================

early_printk("Booting\n")
      |
      v
early_serial_write()
      |
      v
for each char:
      |
      +--> wait for LSR.XMTRDY
      |
      +--> outb(char, COM1 + TXR)
      |
      +--> if '\n', send '\r'


===============================================================================
WHY THIS FILE IS IMPORTANT
===============================================================================

This file is often the only way to debug early boot failures.

Useful when debugging:

    page table setup
    memory map setup
    APIC setup
    ACPI setup
    bootmem
    SMP bringup
    kernel decompression/entry issues


Without early printk:

    early kernel panic may show nothing.


===============================================================================
RELATION TO PREVIOUS FILES
===============================================================================

e820.c
    |
    +--> may use early_printk during early memory-map failure

apic.c
    |
    +--> APIC debugging can be printed early

acpi.c / wakeup.S
    |
    +--> resume debugging may print through early console

crash.c
    |
    +--> crash path usually cannot rely on full console


===============================================================================
KEY IDEA
===============================================================================

early_printk is a temporary emergency console.

It bypasses normal console complexity and writes directly to very simple
hardware:

    VGA memory
    serial port I/O registers
    simulator hook

It exists so the kernel can speak before the real console subsystem is alive.
===============================================================================
```

