.
├── boot
│   ├── compressed
│   │   ├── Makefile
│   │   └── misc.c
│   ├── install.sh
│   ├── Makefile
│   ├── mtools.conf.in
│   └── tools
│       └── build.c
├── crypto
│   ├── aes.c
│   ├── Makefile
│   └── twofish.c
├── defconfig
├── ia32
│   ├── audit.c
│   ├── fpu32.c
│   ├── ia32_aout.c
│   ├── ia32_binfmt.c
│   ├── ia32_signal.c
│   ├── ipc32.c
│   ├── Makefile
│   ├── mmap32.c
│   ├── ptrace32.c
│   ├── sys_ia32.c
│   ├── syscall32.c
│   ├── tls32.c
│   └── vsyscall.lds
├── Kconfig
├── Kconfig.debug
├── kernel
│   ├── acpi
│   │   ├── Makefile
│   │   └── sleep.c
│   ├── aperture.c
│   ├── apic.c
│   ├── asm-offsets.c
│   ├── audit.c
│   ├── cpufreq
│   │   ├── Kconfig
│   │   └── Makefile
│   ├── crash_dump.c
│   ├── crash.c
│   ├── e820.c
│   ├── early_printk.c
│   ├── early-quirks.c
│   ├── functionlist
│   ├── genapic_cluster.c
│   ├── genapic_flat.c
│   ├── genapic.c
│   ├── head64.c
│   ├── i387.c
│   ├── i8259.c
│   ├── init_task.c
│   ├── io_apic.c
│   ├── ioport.c
│   ├── irq.c
│   ├── k8.c
│   ├── kprobes.c
│   ├── ldt.c
│   ├── machine_kexec.c
│   ├── Makefile
│   ├── mce_amd.c
│   ├── mce_intel.c
│   ├── mce.c
│   ├── module.c
│   ├── mpparse.c
│   ├── nmi.c
│   ├── pci-calgary.c
│   ├── pci-dma.c
│   ├── pci-gart.c
│   ├── pci-nommu.c
│   ├── pci-swiotlb.c
│   ├── pmtimer.c
│   ├── process.c
│   ├── ptrace.c
│   ├── reboot.c
│   ├── setup.c
│   ├── setup64.c
│   ├── signal.c
│   ├── smp.c
│   ├── smpboot.c
│   ├── stacktrace.c
│   ├── suspend.c
│   ├── sys_x86_64.c
│   ├── syscall.c
│   ├── tce.c
│   ├── time.c
│   ├── traps.c
│   ├── vsmp.c
│   ├── vsyscall.c
│   └── x8664_ksyms.c
├── lib
│   ├── bitops.c
│   ├── bitstr.c
│   ├── csum-partial.c
│   ├── csum-wrappers.c
│   ├── delay.c
│   ├── io.c
│   ├── Makefile
│   ├── memmove.c
│   └── usercopy.c
├── Makefile
├── mm
│   ├── extable.c
│   ├── fault.c
│   ├── init.c
│   ├── ioremap.c
│   ├── k8topology.c
│   ├── Makefile
│   ├── mmap.c
│   ├── numa.c
│   ├── pageattr.c
│   └── srat.c
├── NOTES.md
├── oprofile
│   ├── Kconfig
│   └── Makefile
└── pci
    ├── k8-bus.c
    ├── Makefile
    └── mmconfig.c

13 directories, 110 files
