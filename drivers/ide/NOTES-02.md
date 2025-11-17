drivers/ide
├── arm/ (ARM-specific IDE controllers)
│   ├── bast-ide.c  # IDE controller driver for the Bast ARM evaluation board.
│   ├── icside.c    # IDE driver for the ICS IDE controller on ARM platforms.
│   ├── ide_arm.c   # Core initialization and support code for ARM IDE interfaces.
│   ├── Makefile    # Build rules and configuration for ARM-specific IDE drivers.
│   └── rapide.c    # Driver for the Rapid E IDE expansion board on ARM.
├── cris/ (CRIS architecture IDE support)
│   ├── ide-cris.c  # IDE support tailored for Etrax CRIS architecture.
│   └── Makefile    # Build configuration for CRIS IDE drivers.
├── h8300/ (H8/300 microcontroller IDE)
│   └── ide-h8300.c # IDE driver for Renesas H8/300 microcontroller series.
├── ide-cd.c          # ATAPI CD-ROM drive handling within the IDE subsystem.
├── ide-cd.h          # Header definitions for IDE CD-ROM structures and functions.
├── ide-disk.c        # Primary driver for IDE hard disk drives and partitions.
├── ide-dma.c         # Direct Memory Access (DMA) engine for IDE transfers.
├── ide-floppy.c      # ATAPI floppy disk support over IDE interface.
├── ide-generic.c     # Fallback generic IDE host controller implementation.
├── ide-io.c          # Low-level I/O port read/write operations for IDE.
├── ide-iops.c        # IDE I/O request processing and queue management.
├── ide-lib.c         # Shared utility functions for the IDE layer.
├── ide-pnp.c         # Plug-and-Play detection and setup for IDE devices.
├── ide-probe.c       # Probing logic to detect and initialize IDE hardware.
├── ide-proc.c        # /proc filesystem interface for IDE device stats and config.
├── ide-tape.c        # ATAPI tape drive support for IDE-connected streamers.
├── ide-taskfile.c    # Handling of ATA taskfile registers and commands.
├── ide-timing.h      # Definitions for IDE/ATA timing parameters and calculations.
├── ide.c             # Central IDE core module coordinating controllers and drives.
├── Kconfig           # Kernel configuration menu options for IDE features.
├── legacy/ (Legacy platform and chipset IDE drivers)
│   ├── ali14xx.c    # Driver for legacy ALi M14xx/15xx IDE chipsets.
│   ├── buddha.c     # Amiga Buddha IDE Zorro-II expansion card driver.
│   ├── dtc2278.c    # Legacy DTC 2278 SCSI/IDE host adapter driver.
│   ├── falconide.c  # IDE driver for Atari Falcon computer.
│   ├── gayle.c      # Amiga 1200/4000 Gayle IDE chip driver.
│   ├── hd.c         # Minimal legacy hard disk geometry setup code.
│   ├── ht6560b.c    # HT Technology HT6560B IDE controller driver.
│   ├── ide-cs.c     # PCMCIA/CardBus IDE adapter support (legacy).
│   ├── macide.c     # Legacy IDE driver for classic Macintosh models.
│   ├── Makefile     # Build setup for legacy platform-specific IDE drivers.
│   ├── q40ide.c     # IDE emulation driver for Q40 (Qemu) virtual machine.
│   ├── qd65xx.c     # QD65xx IDE controller driver for Amiga.
│   ├── qd65xx.h     # Header file for QD65xx Amiga IDE support.
│   └── umc8672.c    # UMC U8672 IDE chipset driver (legacy).
├── Makefile          # Top-level Makefile for compiling IDE drivers.
├── mips/ (MIPS architecture IDE support)
│   ├── au1xxx-ide.c # IDE driver for Au1xxx (Au1000/1100/1500) MIPS processors.
│   ├── Makefile     # Build configuration for MIPS IDE drivers.
│   └── swarm.c      # IDE driver for the Swarm (Jazz) MIPS workstation.
├── NOTES.md          # Markdown notes and documentation on IDE driver usage and quirks.
├── pci/ (PCI-based IDE controllers)
│   ├── aec62xx.c       # Acer Labs AEC62xx PCI IDE controller driver.
│   ├── alim15x3.c      # ALi M15x3 PCI IDE host controller driver.
│   ├── amd74xx.c       # AMD 756/766 IDE controller (74xx series) driver.
│   ├── atiixp.c        # ATI IXP (SB200/SB300) PCI IDE controller driver.
│   ├── cmd640.c        # CMD Technology CMD640 PCI IDE chipset driver.
│   ├── cmd64x.c        # CMD64x series PCI IDE controller driver.
│   ├── cs5520.c        # Cyrix/NS 5520 PCI IDE controller driver.
│   ├── cs5530.c        # Cyrix/NS 5530 PCI IDE controller driver.
│   ├── cs5535.c        # Cyrix/NS 5535/5536 PCI IDE controller driver.
│   ├── cy82c693.c      # Cyrix 82C693 PCI IDE/UART controller driver.
│   ├── generic.c       # Generic PCI IDE interface detection and setup.
│   ├── hpt34x.c        # HighPoint HPT343/344 PCI IDE controller driver.
│   ├── hpt366.c        # HighPoint HPT366/372 PCI IDE controller driver.
│   ├── it821x.c        # ITE IT821x PCI IDE controller driver.
│   ├── jmicron.c       # JMicron JMB360/361/362/363/364/365/366/368 PCI IDE/SATA driver.
│   ├── Makefile        # Build configuration for PCI-based IDE drivers.
│   ├── ns87415.c       # National Semiconductor PC87415 PCI IDE controller driver.
│   ├── opti621.c       # OPTi 82C621 PCI IDE controller driver.
│   ├── pdc202xx_new.c  # Promise PDC202xx (newer) PCI IDE controller driver.
│   ├── pdc202xx_old.c  # Promise PDC202xx (older) PCI IDE controller driver.
│   ├── piix.c          # Intel PIIX/ICH PCI IDE controller driver.
│   ├── rz1000.c        # RZ1000 PCI IDE controller driver.
│   ├── sc1200.c        # ServerWorks CS1212/SC1200 PCI IDE controller driver.
│   ├── serverworks.c   # ServerWorks CSB4/5/6/ROS PCI IDE controller driver.
│   ├── sgiioc4.c       # SGI IOC4 PCI IDE controller driver.
│   ├── siimage.c       # Silicon Image SiI 0680/3112/3512 PCI IDE/SATA driver.
│   ├── sis5513.c       # SiS 5513/5595 PCI IDE controller driver.
│   ├── sl82c105.c      # Symbios Logic 82C105 PCI IDE controller driver.
│   ├── slc90e66.c      # EFAR SLC90E66 PCI IDE controller driver.
│   ├── triflex.c       # Intel 450NX (TriFlex) PCI IDE controller driver.
│   ├── trm290.c        # Tekram TRM290 PCI IDE controller driver.
│   └── via82cxxx.c     # VIA 82Cxxx PCI IDE controller driver series.
├── ppc/ (PowerPC architecture IDE support)
│   ├── mpc8xx.c  # IDE driver for Freescale MPC8xx PowerPC processors.
│   └── pmac.c    # PowerMac (Apple) IDE controller driver.
└── setup-pci.c   # PCI resource allocation and setup for IDE controllers.
