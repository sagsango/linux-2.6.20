LINUX 2.6.20 DRIVERS READING MIND MAP
=====================================

Root:
  /drivers
  /sound        <-- not inside drivers/, but treat as an important driver subsystem


============================================================
0. MASTER READING ORDER
============================================================

drivers/base
    |
    v
drivers/pci
    |
    v
drivers/char
    |
    v
drivers/block
    |
    v
drivers/ide
    |
    v
drivers/scsi
    |
    v
drivers/net
    |
    v
drivers/input
    |
    v
drivers/usb
    |
    v
drivers/serial
    |
    v
drivers/i2c
    |
    v
sound/core + sound/pci
    |
    v
drivers/acpi / cpufreq / clocksource / rtc / mtd / dma


============================================================
1. drivers/base/  -- FIRST READ THIS
============================================================

Purpose:
  This is the Linux driver model.

Main concepts:
  struct device
  struct device_driver
  struct bus_type
  struct class
  platform_device
  platform_driver
  probe()
  remove()
  sysfs device tree

Important files:
  drivers/base/core.c
  drivers/base/bus.c
  drivers/base/driver.c
  drivers/base/class.c
  drivers/base/platform.c

Mind map:

  driver model
      |
      +--> device
      |       |
      |       +--> physical/logical device object
      |       +--> appears in sysfs
      |
      +--> driver
      |       |
      |       +--> code that controls device
      |       +--> has probe/remove callbacks
      |
      +--> bus
      |       |
      |       +--> matches device with driver
      |       +--> examples: PCI, USB, platform
      |
      +--> class
              |
              +--> user-visible category
              +--> examples: block, net, tty, sound

Generic flow:

  device_register()
        |
        v
  bus tries to match device with driver
        |
        v
  if match found
        |
        v
  driver->probe(device)


============================================================
2. drivers/pci/  -- HARDWARE DISCOVERY
============================================================

Purpose:
  PCI discovers real hardware and calls matching drivers.

Important files:
  drivers/pci/probe.c
  drivers/pci/pci.c
  drivers/pci/bus.c
  drivers/pci/driver.c
  drivers/pci/setup-res.c

Main structures:
  struct pci_dev
  struct pci_driver
  struct pci_device_id

Flow:

  PCI bus scan
      |
      v
  read config space
      |
      v
  create struct pci_dev
      |
      v
  match pci_device_id table
      |
      v
  call pci_driver->probe()
      |
      v
  actual device driver initializes hardware

Example drivers using PCI:
  drivers/net/e100.c
  drivers/net/8139too.c
  drivers/ide/pci/*.c
  sound/pci/hda/hda_intel.c


============================================================
3. drivers/char/  -- SIMPLE /dev FILE OPERATIONS
============================================================

Purpose:
  Best place to learn simple device files.

Important files:
  drivers/char/mem.c
  drivers/char/random.c
  drivers/char/tty_io.c

Main concepts:
  major number
  minor number
  struct file_operations
  open()
  read()
  write()
  ioctl()
  mmap()

Flow:

  userspace
      |
      | open("/dev/xxx")
      | read()
      | write()
      | ioctl()
      v
  VFS
      |
      v
  chrdev layer
      |
      v
  file_operations callback
      |
      v
  driver code

Example:

  /dev/mem
  /dev/null
  /dev/zero
  /dev/random
  /dev/tty


============================================================
4. drivers/block/  -- BLOCK DEVICE BASICS
============================================================

Purpose:
  Learn disk-like devices and request queues.

Important files:
  drivers/block/loop.c
  drivers/block/rd.c

Main concepts:
  struct gendisk
  struct request_queue
  struct request
  struct bio
  request_fn
  elevator / IO scheduler

Flow:

  userspace read/write file
      |
      v
  VFS
      |
      v
  filesystem
      |
      v
  page cache
      |
      v
  block layer
      |
      v
  request queue
      |
      v
  block driver
      |
      v
  disk/device


============================================================
5. drivers/ide/  -- OLD STORAGE STACK IN 2.6.20
============================================================

Purpose:
  Very important for Linux 2.6.20 storage.

Important files:
  drivers/ide/ide.c
  drivers/ide/ide-disk.c
  drivers/ide/ide-dma.c
  drivers/ide/pci/*.c

Main concepts:
  IDE disk
  request handling
  DMA setup
  interrupt completion
  PCI IDE controller

Flow:

  /dev/hda or /dev/hdX
      |
      v
  block layer
      |
      v
  IDE core
      |
      v
  IDE disk driver
      |
      v
  IDE PCI controller
      |
      v
  DMA transfer
      |
      v
  disk interrupt
      |
      v
  request complete


============================================================
6. drivers/scsi/  -- SCSI MID-LAYER
============================================================

Purpose:
  Understand /dev/sda style disk flow.

Important files:
  drivers/scsi/scsi.c
  drivers/scsi/scsi_lib.c
  drivers/scsi/sd.c

Main concepts:
  SCSI command
  SCSI host
  SCSI disk
  SCSI midlayer
  host adapter driver

Flow:

  /dev/sda
      |
      v
  block layer
      |
      v
  sd.c
      |
      v
  SCSI midlayer
      |
      v
  host adapter driver
      |
      v
  hardware


============================================================
7. drivers/net/  -- NETWORK DRIVERS
============================================================

Purpose:
  Very important subsystem for packets, interrupts, DMA, skb.

Start with:
  drivers/net/e100.c
  drivers/net/8139too.c
  drivers/net/tun.c

Main concepts:
  struct net_device
  struct sk_buff
  open()
  stop()
  hard_start_xmit()
  interrupt handler
  RX path
  TX path
  NAPI

TX flow:

  userspace send()
      |
      v
  socket layer
      |
      v
  TCP/IP stack
      |
      v
  dev_queue_xmit()
      |
      v
  net_device->hard_start_xmit()
      |
      v
  NIC driver
      |
      v
  DMA to hardware
      |
      v
  packet on wire

RX flow:

  packet arrives at NIC
      |
      v
  interrupt
      |
      v
  NIC driver
      |
      v
  allocate/fill sk_buff
      |
      v
  netif_rx()
      |
      v
  protocol stack
      |
      v
  socket receive queue
      |
      v
  userspace recv()


============================================================
8. drivers/input/  -- KEYBOARD / MOUSE FLOW
============================================================

Purpose:
  Understand input devices and event delivery.

Important files:
  drivers/input/input.c
  drivers/input/keyboard/atkbd.c
  drivers/input/mouse/psmouse-base.c
  drivers/input/serio/i8042.c

Main concepts:
  input_dev
  input_register_device()
  input_event()
  serio
  evdev
  keyboard driver
  mouse driver

Keyboard flow:

  key press
      |
      v
  keyboard controller i8042
      |
      v
  interrupt
      |
      v
  serio layer
      |
      v
  atkbd driver
      |
      v
  input core
      |
      +--> console
      |
      +--> evdev
              |
              v
          /dev/input/eventX
              |
              v
          X11 / userspace


============================================================
9. drivers/usb/  -- DYNAMIC DEVICE ENUMERATION
============================================================

Purpose:
  Learn hotplug, enumeration, device descriptors, USB drivers.

Important files:
  drivers/usb/core/usb.c
  drivers/usb/core/hub.c
  drivers/usb/core/message.c
  drivers/usb/storage/
  drivers/usb/input/

Main concepts:
  usb_device
  usb_interface
  usb_driver
  URB
  endpoint
  control/bulk/interrupt transfer
  enumeration

USB plug-in flow:

  device plugged in
      |
      v
  hub interrupt/event
      |
      v
  reset port
      |
      v
  read device descriptor
      |
      v
  assign USB address
      |
      v
  read configuration descriptor
      |
      v
  create usb_interface
      |
      v
  match usb_driver
      |
      v
  driver->probe()


============================================================
10. drivers/serial/  -- UART / TTY DRIVER
============================================================

Purpose:
  Good for interrupt-driven character device and console path.

Important files:
  drivers/serial/8250.c
  drivers/serial/serial_core.c

Main concepts:
  UART
  tty layer
  console
  interrupt RX/TX
  baud rate
  serial port registers

Flow:

  userspace writes to /dev/ttyS0
      |
      v
  tty layer
      |
      v
  serial core
      |
      v
  8250 driver
      |
      v
  UART hardware


============================================================
11. drivers/i2c/  -- SMALL DEVICE BUS
============================================================

Purpose:
  Learn adapter/client/driver model for low-speed devices.

Important files:
  drivers/i2c/i2c-core.c
  drivers/i2c/busses/
  drivers/i2c/chips/

Main concepts:
  i2c_adapter
  i2c_client
  i2c_driver
  probe()
  SMBus read/write

Flow:

  i2c bus controller
      |
      v
  i2c_adapter
      |
      v
  i2c_client device
      |
      v
  i2c_driver->probe()
      |
      v
  device-specific driver


============================================================
12. sound/  -- AUDIO DRIVER STACK
============================================================

Note:
  Audio is not under drivers/ in this tree.
  But it is a major driver subsystem.

Important files:
  sound/core/sound.c
  sound/core/pcm.c
  sound/core/pcm_native.c
  include/sound/core.h
  include/sound/pcm.h
  sound/pci/hda/hda_intel.c
  sound/pci/ac97/

Main concepts:
  ALSA
  snd_card
  snd_pcm
  snd_pcm_substream
  snd_pcm_runtime
  snd_pcm_ops
  DMA ring buffer
  period interrupt

Playback flow:

  userspace app
      |
      v
  libasound
      |
      v
  open("/dev/snd/pcmC0D0p")
      |
      v
  VFS
      |
      v
  ALSA core
      |
      v
  PCM core
      |
      v
  hardware driver's snd_pcm_ops
      |
      v
  DMA buffer
      |
      v
  audio hardware
      |
      v
  speaker


============================================================
13. drivers/acpi/  -- FIRMWARE / POWER / DEVICE INFO
============================================================

Purpose:
  ACPI exposes firmware tables, devices, power methods.

Read after:
  base, pci, platform drivers

Important areas:
  drivers/acpi/
  drivers/acpi/scan.c
  drivers/acpi/bus.c

Flow:

  firmware ACPI tables
      |
      v
  ACPI namespace scan
      |
      v
  ACPI device object
      |
      v
  platform/ACPI driver match
      |
      v
  driver probe


============================================================
14. drivers/cpufreq/ clocksource/ rtc/
============================================================

Purpose:
  Time and CPU power management.

Read:
  drivers/cpufreq/
  drivers/clocksource/
  drivers/rtc/

Concepts:
  CPU frequency scaling
  clocksource
  timer
  real-time clock
  timekeeping support


============================================================
15. drivers/mtd/ and drivers/dma/
============================================================

drivers/mtd:
  Flash storage devices.
  Useful for embedded systems.

drivers/dma:
  DMA engine framework.
  Useful after you understand PCI/block/net/audio DMA.


============================================================
FINAL SHORT ORDER
============================================================

If you want the most productive order:

  1. drivers/base
  2. drivers/pci
  3. drivers/char
  4. drivers/block
  5. drivers/ide
  6. drivers/scsi
  7. drivers/net
  8. drivers/input
  9. drivers/usb
 10. drivers/serial
 11. drivers/i2c
 12. sound/core
 13. sound/pci
 14. drivers/acpi
 15. drivers/cpufreq
 16. drivers/clocksource
 17. drivers/rtc
 18. drivers/mtd
 19. drivers/dma


============================================================
FIRST 20 FILES TO READ
============================================================

  01. drivers/base/core.c
  02. drivers/base/bus.c
  03. drivers/base/driver.c
  04. drivers/base/class.c
  05. drivers/base/platform.c

  06. drivers/pci/probe.c
  07. drivers/pci/driver.c
  08. drivers/pci/pci.c

  09. drivers/char/mem.c
  10. drivers/char/random.c

  11. drivers/block/loop.c

  12. drivers/ide/ide.c
  13. drivers/ide/ide-disk.c
  14. drivers/ide/ide-dma.c

  15. drivers/scsi/scsi.c
  16. drivers/scsi/scsi_lib.c
  17. drivers/scsi/sd.c

  18. drivers/net/e100.c
  19. drivers/net/8139too.c
  20. drivers/net/tun.c


============================================================
CORE IDEA
============================================================

Most Linux drivers follow this pattern:

  bus discovers device
      |
      v
  core creates device object
      |
      v
  driver matches device
      |
      v
  probe() runs
      |
      v
  driver registers user-visible interface
      |
      +--> char device
      +--> block device
      +--> net_device
      +--> input device
      +--> ALSA device
      +--> tty device
      +--> sysfs files
      |
      v
  userspace uses device
      |
      v
  syscall enters kernel
      |
      v
  subsystem core
      |
      v
  driver callback
      |
      v
  hardware interrupt / DMA / MMIO / PIO

