
LINUX DRIVER MODEL - WHY IS id_table[] AN ARRAY?
================================================

CORE IDEA
---------

One Linux driver is designed to support an entire FAMILY of hardware,
not just a single device.

Instead of:

    One Device
        |
        v
    One Driver

Linux prefers:

        One Driver
            |
    +-------+-------+-------+
    |       |       |       |
    v       v       v       v
 DeviceA DeviceB DeviceC DeviceD

============================================================
WHY AN ARRAY OF DEVICE IDs?
============================================================

Bad Design

    e100_100e.c
    e100_100f.c
    e100_1010.c

Three almost identical drivers.

Better Design

    e100.c

supports

    100e
    100f
    1010

Less duplicated code.
Easier maintenance.
One bug fix benefits every supported device.

============================================================
DEVICE ID TABLE
============================================================

static struct pci_device_id ids[] = {

    { PCI_DEVICE(0x8086, 0x100e) },

    { PCI_DEVICE(0x8086, 0x100f) },

    { PCI_DEVICE(0x8086, 0x1010) },

    { 0, }
};

Each entry describes ONE supported device.

The final {0,} marks the end of the table.

============================================================
BOOT-TIME MATCHING
============================================================

PCI Core discovers devices

        |
        v

+---------------------------+
|8086:100e                  |
|8086:1237                  |
|10ec:8139                  |
+---------------------------+

        |

Compare against driver's id_table[]

        |

+---------------------------+
|8086:100e   MATCH          |
|8086:100f                  |
|8086:1010                  |
+---------------------------+

        |

driver->probe()

Kernel conceptually does:

for_each_pci_device()
    for_each_id(driver->id_table)
        if (match)
            probe()

============================================================
ONE DRIVER, MANY PRODUCTS
============================================================

Example:

e1000 driver

supports

    82540EM
    82545EM
    82546EB
    82571EB
    82572EI
    82573E
    ...

Often hundreds of IDs.

============================================================
driver_data
============================================================

ID table entries may contain private data.

Example:

{
    PCI_DEVICE(0x1234,0x1111),
    .driver_data = TYPE_A,
}

{
    PCI_DEVICE(0x1234,0x2222),
    .driver_data = TYPE_B,
}

probe()

    switch(id->driver_data)

        TYPE_A

        TYPE_B

One source file can initialize different hardware
variants differently.

============================================================
WHY NOT MATCH ONLY VENDOR?
============================================================

Intel (Vendor 8086) manufactures many devices:

    Ethernet
    Wi-Fi
    Audio
    SATA
    USB
    Graphics

Vendor ID alone is NOT enough.

Correct match is:

Vendor ID + Device ID

============================================================
OTHER BUSES USE THE SAME IDEA
============================================================

PCI

    struct pci_device_id ids[]

USB

    struct usb_device_id ids[]

Platform

    struct platform_device_id ids[]

I2C

    struct i2c_device_id ids[]

Every Linux bus follows the same pattern:

Driver
    |
    +--> Supported Device Table
            |
            v
Bus Core matches devices
            |
            v
probe()

============================================================
ARCHITECTURE
============================================================

               Driver

        +------------------+
        | pci_driver       |
        +------------------+
                 |
                 v
             id_table[]
                 |
      +----------+----------+----------+
      |          |          |          |
      v          v          v          v
   8086:100e 8086:100f 8086:1010 8086:1011
                 |
                 v
          PCI Core Matching
                 |
        +--------+--------+
        |                 |
      Match           No Match
        |                 |
        v                 |
     probe()      Continue scanning

============================================================
KEY TAKEAWAYS
============================================================

* One driver supports many related devices.
* id_table[] lists every supported device.
* PCI core performs matching; drivers do NOT scan the bus.
* probe() is called only for matching devices.
* driver_data allows one driver to handle hardware variants.
* The same design is used by PCI, USB, I2C, Platform and other buses.

