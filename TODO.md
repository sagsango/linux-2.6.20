
LINUX 2.6.20 SOUND SUBSYSTEM
COMPLETE SOURCE READING ROADMAP
================================

Goal:
    Understand the entire Linux 2.6.20 sound subsystem from the
    generic framework down to the hardware drivers.

====================================================================
                         HIGH LEVEL ARCHITECTURE
====================================================================

                     Userspace
                         |
          libasound / OSS applications
                         |
        open() ioctl() read() write() mmap()
                         |
                         v
                sound_core.c
                         |
                         v
                    sound/core/
                         |
      +------------------+------------------+
      |                  |                  |
      v                  v                  v
  PCM / Control      Timer / MIDI      Device Model
                         |
                         v
                    ac97_bus.c
                         |
                         v
      +--------+--------+--------+--------+--------+
      |        |        |        |        |        |
      v        v        v        v        v        v
     pci/    usb/     isa/   pcmcia/    i2c/   drivers/
                         |
                         v
                    Audio Hardware

====================================================================
PHASE 0 : BUILD SYSTEM
====================================================================

01. Kconfig
02. Makefile

Learn:
    - Configuration options
    - Build dependencies
    - How sound objects are linked

====================================================================
PHASE 1 : GENERIC SOUND INFRASTRUCTURE
====================================================================

03. sound_core.c
04. sound_firmware.c
05. last.c

Learn:
    - Sound subsystem initialization
    - Generic sound device registration
    - Firmware helpers

====================================================================
PHASE 2 : ALSA CORE (MOST IMPORTANT)
====================================================================

Read in this order:

06. core/init.c
07. core/memory.c
08. core/device.c
09. core/sound.c
10. core/info.c
11. core/control.c
12. core/hwdep.c
13. core/timer.c
14. core/pcm.c
15. core/pcm_memory.c
16. core/pcm_timer.c
17. core/pcm_native.c
18. core/rawmidi.c
19. core/seq/
20. core/oss/

Main structures:

    struct snd_card
    struct snd_device
    struct snd_pcm
    struct snd_pcm_substream
    struct snd_pcm_runtime
    struct snd_pcm_ops

Understand:

    Device registration
    Control interface
    PCM creation
    PCM runtime
    DMA ring buffer
    Timer
    MIDI
    Sequencer

====================================================================
PHASE 3 : AC97 BUS
====================================================================

21. ac97_bus.c

Then

22. pci/ac97/

Learn:

    Codec discovery
    Mixer
    DAC
    ADC
    Register programming

====================================================================
PHASE 4 : PCI AUDIO DRIVERS
====================================================================

23. pci/

Suggested order:

    intel8x0/
    ac97/
    emu10k1/
    ca0106/
    ice1712/
    remaining PCI drivers

Focus:

    pci_driver
    probe()
    DMA
    IRQ
    snd_pcm_ops implementation

====================================================================
PHASE 5 : USB AUDIO
====================================================================

24. usb/

Learn:

    USB enumeration
    Endpoints
    URBs
    Isochronous transfers
    PCM callbacks

====================================================================
PHASE 6 : ISA AUDIO
====================================================================

25. isa/

Learn:

    Legacy register programming
    DMA
    IRQ
    PIO

====================================================================
PHASE 7 : PCMCIA
====================================================================

26. pcmcia/

Learn:

    CardBus / PCMCIA audio
    Device discovery
    Driver registration

====================================================================
PHASE 8 : I2C AUDIO
====================================================================

27. i2c/

Learn:

    Audio codecs
    Tuners
    EEPROM
    Peripheral devices

====================================================================
PHASE 9 : OSS
====================================================================

28. oss/

Learn:

    Legacy OSS drivers
    OSS API
    ALSA compatibility

====================================================================
PHASE 10 : GENERIC AUDIO HELPERS
====================================================================

29. drivers/

Miscellaneous helper drivers.

====================================================================
PHASE 11 : SYNTHESIZER
====================================================================

30. synth/

Learn:

    MIDI synthesizers
    Wave table support

====================================================================
PHASE 12 : ARCHITECTURE SPECIFIC
====================================================================

31. arm/
32. aoa/
33. mips/
34. parisc/
35. ppc/
36. sparc/

These contain platform-specific audio implementations.
Read only after understanding the generic ALSA framework.

====================================================================
FINAL READING ORDER
====================================================================

01. Kconfig
02. Makefile

03. sound_core.c
04. sound_firmware.c
05. last.c

06. core/

07. ac97_bus.c

08. pci/

09. usb/

10. isa/

11. pcmcia/

12. i2c/

13. oss/

14. drivers/

15. synth/

16. arm/

17. aoa/

18. mips/

19. parisc/

20. ppc/

21. sparc/

====================================================================
TIME INVESTMENT
====================================================================

core/            50%
pci/             20%
usb/              8%
isa/              6%
ac97              5%
oss/              4%
i2c/              2%
drivers/          2%
synth/            1%
arch-specific     2%

====================================================================
EXPERT PATH
====================================================================

Build System
      |
      v
Infrastructure
      |
      v
ALSA Core
      |
      v
AC97 Bus
      |
      v
PCI Drivers
      |
      v
USB Drivers
      |
      v
ISA Drivers
      |
      v
PCMCIA
      |
      v
I2C
      |
      v
OSS
      |
      v
Generic Helpers
      |
      v
Synth
      |
      v
Architecture Specific Drivers

Master the ALSA core before spending significant time on
hardware-specific drivers.
