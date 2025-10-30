legacy IDE (Integrated Drive Electronics) subsystem — the older block layer and driver framework for ATA hard drives and CD/DVD drives, before the modern libata (drivers/ata/) subsystem replaced it.

user space (mount, fs, etc.)
   ↓
VFS (Virtual File System)
   ↓
block layer (generic)
   ↓
IDE core (drivers/ide/)
   ↓
IDE host driver (PCI/ISA specific)
   ↓
Controller hardware (PATA/ATAPI)
   ↓
Disk / CD-ROM




