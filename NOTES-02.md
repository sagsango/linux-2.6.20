Linux Kernel 2.6.20 Boot Process: GIANT In-Depth ASCII Flow Diagram (x86/32-bit, Full Granularity)
====================================================================================================

This GIANT diagram is the most exhaustive breakdown possible for Linux 2.6.20 (sources: linux-2.6.20.tar.bz2, arch/i386/, init/, Documentation/). It includes micro-steps, register ops, memory hex addresses, exact function calls (with line #'s where notable), error checks, 2.6.20 quirks (e.g., boot_pgsz=4MB default, no KASLR/SMEP, O(1) sched with 140 prios, initcalls ~250 total). Timings: Cumulative on ~1GHz P4 w/512MB RAM (~1.5-3s total). 

Legend (Expanded):
- [ASM]: Inline asm snippets (AT&T syntax); [C]: C pseudocode (simplified).
- States: PG=0/1 (Paging), PE=0/1 (Protected Mode), IRQ=0/1 (Interrupts), PREEMPT=0/1 (Preemption).
- Flow: --> Sequential call; | Parallel (threads/IRQs); ^ Loop back; [ERR] = Panic points.
- 2.6.20 Specs: Boot prot 2.05 (reloc OK), HZ=250, SLAB alloc, SysV init, max initrd=4MB, no cgroup v1 full, basic ACPI 2.0.
- Scale: ~50 phases, 200+ sub-steps; scroll horizontally/vertically.

+---------------------------------------------------+     +---------------------------------------------------+     +---------------------------------------------------+
| 1. Bootloader Handoff (GRUB Stage2/2 or LILO)     | --> | 2. Real-Mode Magic Check & Entry (boot/setup.S)   | --> | 3. Real-Mode BSS Zero & Stack Setup (header.S)    |
|    (~0ms; PG=0, PE=0, IRQ=0; BIOS INTs active)    |     |    (~1-5ms; [ASM]; Line ~1-50)                    |     |    (~5-10ms; [ASM]; Line ~200-250)                |
|    - GRUB: multiboot? No -> legacy boot_params    |     |    - Real-mode entry: 0x7C00 + 0x110 (JMP short) |     |    - mov $0x90000, %sp (stack top, 64KB SS:SP)    |
|      fill (0x1F1): magic=0xAA55 (validate)        |     |    - cmpw $0xAA55, setup_sects (header[0x1FE])    |     |    - mov $0x90000, %ss (set SS seg)               |
|    - Load vmlinuz (bzImage) @ PHYS 0x100000       |     |    - je ok; else hlt; jmp $ (infinite loop)       |     |    - call EXT(cld) (clear DF for string ops)      |
|    - Setup header (512B sectors): setup_sects=4   |     |    - mov $msg_real, %si; call puts (BIOS INT10)   |     |    - mov $__bss_start, %di; mov $0, %al; movl     |
|      (real-mode code ~2KB)                        |     |      [msg_real="Loading Linux..."]                |     |      $__bss_stop - __bss_start, %ecx; rep stosb   |
|    - E820: BIOS INT15,E820h -> map[0-32] (ACPI)   |     |    - JMP start_of_setup (0x967: after header)     |     |      (zero ~1KB BSS; ECX=bytes/4)                 |
|      e.g., entry0: base=0x0, len=0x9FC00, type=1  |     |    [ERR: Invalid magic -> hlt forever]            |     |    [2.6.20: BSS includes video_vesa_mode vars]    |
|    - Cmdline: "root=/dev/sda1 ro initrd=0x2000000 |     |    [Rationale: Validate image integrity]          |     |    [Rationale: Clean slate for vars; stack safe]  |
|      0x400000" @0x10000 (after setup)             |     +---------------------------------------------------+     +---------------------------------------------------+
|    - Initrd: If param, note start/size @0x218/21C |             |                                           |
|    - Vid mode: 0xFFFF (probe)                     |             v                                           v
|    - Jump far real,0x7C00 (CS:IP reset)           |    +---------------------------------------------------+     +---------------------------------------------------+
|    [2.6.20: Supports CAN_RELLOAD (loadflags=0x204)|    | 4. Real-Mode Param Copy & Heap Alloc (main.c)     |     | 5. Video Mode Probe & Set (main.c)                |
|     for non-1MB loads; no EFI stub]               |    |    (~10-15ms; [C]; Line ~100-150)                |     |    (~15-20ms; [C]; Line ~200-250)                 |
|    [Rationale: Pass env to kernel; BIOS legacy]   |    |    void main(struct boot_params *bp) {           |     |    - if (bp->screen_info.orig_video_mode==7)      |
+---------------------------------------------------+    |      memcpy(&boot_params, bp, sizeof(*bp));      |     |      probe_cards(0x10); // VESA modes via INT10   |
                                                      |      if (boot_params.hdr.loadflags & CAN_USE_HEAP)|     |    - for (i=0; i<NR_E820; i++) if (type==7)       |
                                                      |        heap_top = malloc(0x1000); // 4KB heap     |     |        vesa_mode = get_vesa_mode(0x101, 1024x768) |
                                                      |      copy_e820map(&bp->e820_map, boot_params.e820)|     |    - set_video(vmode): outb(0x3C2, vmode>>8);     |
                                                      |        (20B/entry x32 max; type1=RAM,7=usable)   |     |      INT 0x10, AX=vmode (e.g., 0x118=1024x768x64k)|
                                                      |    }                                             |     |    [2.6.20: Supports 0xFFFF=probe; no EFI VBE]    |
                                                      |    [Rationale: Persist params to PM; heap for     |     |    [ERR: No mode -> fallback 80x25 text (mode3)] |
                                                      |     long cmdlines >0x90000-0x10000]              |     +---------------------------------------------------+
                                                      +---------------------------------------------------+             |
                                                                      |                                           |
                                                                      v                                           |
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      | 6. Initrd Load & Decompress (main.c)              |     | 7. Protected Mode Prep (go_to_protected_mode)     |
                                                      |    (~20-30ms; [C]; Line ~300-350)                |     |    (pm.c; ~30-40ms; [C]; Line ~50-100)           |
                                                      |    - if (bp->hdr.type_of_loader != 0x21) return;  |     |    void go_to_protected_mode(struct desc_ptr *gdt)|
                                                      |      // Assume GRUB=0x21                           |     |    { mask_pic_irq(0xFFFF); // Disable all IRQs   |
                                                      |    - if (cmdline find "initrd=") {                |     |      outb(0x80, 0x70); // Disable NMI             |
                                                      |        sscanf("%lx %lx", &start, &size);          |     |      load_gdt(gdt); // lgdt [gdt_desc] (48B lim) |
                                                      |        rd_size = load_ramdisk(start, size,        |     |      ljmp 8, $1f; // Far jump to code seg 0x8    |
                                                      |          0x2000000, rd_size); // High load        |     |      1: mov $0x10, %ax; mov %ax, %ds/%es/%ss     |
                                                      |        if (rd_size<0) [ERR: "RAMDISK: Incomplete"]|     |        (Reload data segs; flat 4GB)              |
                                                      |        bp->hdr.ramdisk_image=start; size=rd_size; |     |      sti; // Enable IF but PIC masked             |
                                                      |      }                                            |     |    }                                             |
                                                      |    - Decompress if compressed initrd (gzip only)  |     |    [2.6.20: Temp GDT: null(0x0), code(0x9A),     |
                                                      |      gunzip(0x2000000, size);                     |     |     data(0x92); no TSS yet]                       |
                                                      |    [2.6.20: Max 4MB; no multi-stage; ERR->no panic|     |    [Rationale: Safe 32-bit entry; no PG/CR3 yet]  |
                                                      |     yet, just skip]                               |     +---------------------------------------------------+
                                                      +---------------------------------------------------+             |
                                                                      |                                           |
                                                                      v                                           v
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      | 8. Mode Switch Assembly (pmjump.S)                |     | 9. Compressed Head: VGA Print Start (head_32.S)   |
                                                      |    (~40-50ms; [ASM]; Line ~1-20)                  |     |    (~50-60ms; [ASM/C]; misc.c decompress_kernel) |
                                                      |    .code16                                   |     |    void decompress_kernel(uint32_t load_addr) {  |
                                                      |    protected_mode_jump:                        |     |      write_to_vga("Uncompressing Linux...");     |
                                                      |      cli; mov %cr0, %eax; or $1, %eax; mov %eax, |     |      // Direct 0xB8000 write: mov $msg,%si;      |
                                                      |        %cr0; // PE=1 (no PG yet)                 |     |      // lodsb; mov %al, 0xB8000+ofs; inc ofs x2   |
                                                      |      ljmp $0x08, $1f; // CS switch to long       |     |      gunzip(piggy.o, load_addr=0x100000,         |
                                                      |      1: mov $0x10, %ax; mov %ax, %ds; mov %ax,   |     |        out_len); // bz2/gzip from vmlinux.bin    |
                                                      |        %es; mov %ax, %fs; mov %ax, %gs; mov %ax, |     |      if (boot_params.hdr.loadflags & LOADED_HIGH)|
                                                      |        %ss; // All data segs flat                 |     |        relocate_kernel(0x1000000); // Stub adjust|
                                                      |      mov $0x100000, %eax; jmp *%eax; // To comp  |     |      write_to_vga(" ... done.\nBootup debug...");|
                                                      |    [ERR: Triple fault if GDT bad -> reboot]      |     |      jump_to_uncompressed(0x100000);             |
                                                      |    [Rationale: Atomic PE enable; no interrupts]  |     |    }                                             |
                                                      +---------------------------------------------------+     |    [2.6.20: Reloc if !0x100000 (rare); basic      |
                                                                      |             gunzip, no lzma/zstd]                 |
                                                                      v             +---------------------------------------------------+
                                                      +---------------------------------------------------+             |
                                                      |10. Uncompressed Head: BSS Zero Redux (head.S)    |             |
                                                      |    (~60-70ms; [ASM]; Line ~50-100)                |             v
                                                      |    .section .text                                |    +---------------------------------------------------+
                                                      |    startup_32:                                   |    |11. Page Table Construction (head.S)               |
                                                      |      cld;                                        |    |    (~70-100ms; [ASM]; Line ~150-250)              |
                                                      |      movl $(__bss_stop - __bss_start), %ecx;     |    |    - mov $pg0, %edi; mov $0x1000, %ecx; xor %eax, |
                                                      |      xorl %eax,%eax; rep; stosl; // Zero ~8MB    |    |        %eax; rep stosl; // Zero pgd @0x2000 (4KB)|
                                                      |        (includes .data, .bss for full kernel)    |    |    - mov $0x10000000 >> 22, %ebx; // PDE idx for |
                                                      |    [2.6.20: __bss_start=0xC0100000 virt post-map]|    |      high kernel map (0xC0000000 virt)            |
                                                      |    [Rationale: Full kernel clean; overlaps comp] |    |    - fill_low_pte_table(0, 0x1000, 0x83); // Low  |
                                                      |                                                      |    |      identity: 0-8MB, 4KB pages, P=1,RW=1,U=0,X=1|
                                                      |                                                      |    |    - fill_pte_table(0xC00, 0x1000, 0x83, 0x100000)|
                                                      |                                                      |    |      // Kernel text/data highmap (8MB)           |
                                                      |                                                      |    |    - If PAE (CPUID 0x80000001 EDX[6]):           |
                                                      |                                                      |    |      mov %cr4, %eax; or $0x20, %eax; mov %eax,   |
                                                      |                                                      |    |        %cr4; // PAE=1 for >4GB                   |
                                                      |                                                      |    |    [2.6.20: Default 4MB pgsz; opt CONFIG_HIGHMEM]|
                                                      +---------------------------------------------------+    |    [Rationale: Identity for early code; high for  |
                                                                      |             kernel seg; no NX bit]               |
                                                                      |             +---------------------------------------------------+
                                                                      v                                           |
                                                      +---------------------------------------------------+             |
                                                      |12. GDT/IDT Load & Paging Enable (head.S)         |             |
                                                      |    (~100-120ms; [ASM]; Line ~250-300)             |             v
                                                      |    - lgdt gdt_descr; // Final GDT @0x800 (48B):  |    +---------------------------------------------------+
                                                      |      null(0), kcode(0x9A: DPL0,32b,4GB), kdata   |    |13. Stack/TSS Setup & C Jump (head.S)              |
                                                      |      (0x92), udata(0xFA: DPL3,32b,3GB), tss(0x89)|    |    (~120-130ms; [ASM]; Line ~300-350)             |
                                                      |    - lidt idt_descr; // Early IDT @0 (48B: 48   |    |    - movl $empty_zero_page - 4, %eax; // Stack   |
                                                      |      entries); set_intr_gate(0, early_idt);      |    |      bottom guard @0xC0300000 -4                  |
                                                      |        e.g., divide_error: offset=divide_error   |    |    - movl %eax, %ss; movl $__STACK_END-4, %esp;  |
                                                      |    - mov $pg0+0x1000, %eax; mov %eax, %cr3; // PGD|    |      // init_task_union.thread.esp = 0xC0303FFC   |
                                                      |    - mov %cr0, %eax; or $0x80000000, %eax; // PG=1|    |    - ltr $0x10; // Load TR=0 (TSS seg for proc0)|
                                                      |      mov %eax, %cr0; jmp 1f; // Flush TLB        |    |    - call *%cs:initial_code; // To C stub        |
                                                      |      1: mov $1f, %eax; jmp *%eax; // Far jmp reload|    |      initial_code = start_kernel (init/main.c)    |
                                                      |        CS after PG on                            |    |    [2.6.20: TSS basic; no per-CPU TSS yet]        |
                                                      |    [ERR: #PF on bad map -> triple fault reboot]  |    |    [Rationale: Proc0 ready; jump to C world]     |
                                                      |    [2.6.20: CR0.WP=1 set here; no CR0.NE]        |    +---------------------------------------------------+
                                                      +---------------------------------------------------+             |
                                                                      |                                           |
                                                                      v                                           v
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      |14. start_kernel() Entry (init/main.c)            |     |15. Lock & Banner Print (start_kernel)             |
                                                      |    (~130-140ms; [C]; Line ~50-100)               |     |    (~140-150ms; [C]; Line ~100-150)               |
                                                      |    asmlinkage void start_kernel(void) {          |     |    - lock_kernel(); // spin_lock(&kernel_flag)   |
                                                      |      char *from = __va(boot_params_phys);        |     |        (SMP safe; single CPU ok)                  |
                                                      |      char *to   = __va(0);                       |     |    - console_loglevel = 7; // Full verbose        |
                                                      |      memcpy(to, from, sizeof(boot_params));      |     |    - printk(KERN_INFO "Linux version 2.6.20.%s ",|
                                                      |        // Phys-to-virt copy params (0x90000->0)  |     |        UTS_RELEASE); // e.g., .gcc4.1.1          |
                                                      |      system_state = SYSTEM_BOOTING;              |     |    - dump_stack(); // Optional backtrace          |
                                                      |      /* Subcalls below */                        |     |    [2.6.20: No smp_processor_id() early; basic    |
                                                      |    }                                             |     |     printk via direct console (no tty yet)]       |
                                                      |    [Rationale: State machine start; params safe] |     +---------------------------------------------------+
                                                      +---------------------------------------------------+             |
                                                                      |                                           |
                                                                      v                                           |
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      |16. setup_arch(&command_line) (setup.c)           |     |17. CPU Feature Detect & CR4 Set (setup.c)         |
                                                      |    (~150-200ms; [C]; Line ~200-300)              |     |    (~200-220ms; [C]; Line ~300-350)               |
                                                      |    void setup_arch(char **cmdline_p) {           |     |    - identify_cpu(&boot_cpu_data); // CPUID 0x0   |
                                                      |      *cmdline_p = boot_command_line;             |     |      -> family=15 (P4), model=3, stepping=4       |
                                                      |      parse_early_param(); // strstr("noapic",..) |     |    - if (test_bit(X86_FEATURE_PAE)) cr4 |= PAE;   |
                                                      |      root_mountflags &= ~MS_RDONLY;              |     |    - mov %cr4, %eax; mov %eax, %cr4; // Enable    |
                                                      |        (from "ro" param)                         |     |      features: FXSR=1 (FPU), PGE=1 (global pages)|
                                                      |      find_tsc_pit_calibrate(); // Early time     |     |    - alternative_instructions(); // Opt vendor   |
                                                      |    }                                             |     |      patch (Intel/AMD/..)                          |
                                                      |    [2.6.20: No reserved_mem; basic cmd parse]    |     |    [2.6.20: Supports SSE/SSE2; no SSSE3 detect]   |
                                                      +---------------------------------------------------+     |    [Rationale: Arch-specific; enable FPU early]   |
                                                                      |             +---------------------------------------------------+
                                                                      v                                           |
                                                      +---------------------------------------------------+             |
                                                      |18. Memory Init (setup.c mem_init)                |             v
                                                      |    (~220-250ms; [C]; Line ~400-500)              |    +---------------------------------------------------+
                                                      |    - bootmem_init(); // free_bootmem(0, max_low) |    |19. Per-CPU & SMP Prep (setup.c)                    |
                                                      |      max_low_pfn = max_low_pfn_mapped << (PAGE_SHIFT-12)|    |    (~250-270ms; [C]; Line ~500-550)               |
                                                      |    - build_all_zonelists(NULL); // DMA(0-16MB),  |    |    - setup_per_cpu_areas(); // pcpu_base_addr=0  |
                                                      |      Normal(16MB-max)                            |    |    - smp_prepare_cpus(max_cpus=1); // APIC probe  |
                                                      |    - mem_map = alloc_bootmem_pages(PFN_PHYS(max) |    |      if (smp_found_config) max_cpus=NR_CPUS;      |
                                                      |        / sizeof(struct page));                   |    |    [2.6.20: UP default; no x2apic; basic ACPI MADT|
                                                      |    - for (i=0; i<max_pfn; i++) set_page_count(..)|    |     parse for SMP]                                 |
                                                      |    [2.6.20: No memblock; bootmem for early alloc]|    +---------------------------------------------------+
                                                      |    [ERR: Out of mem -> "Out of memory" early panic]|
                                                      +---------------------------------------------------+             |
                                                                      |                                           |
                                                                      v                                           |
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      |20. Early Console Init (console_init)             |     |21. Trap Init (trap_init)                            |
                                                      |    (~270-280ms; [C]; Line ~init/main.c 150)      |     |    (~280-300ms; [C]; trap_init.c Line ~50-200)    |
                                                      |    - register_console(&vga_con); // If vga=ask   |     |    extern void (*interrupt[])(void);              |
                                                      |    - if (earlyprintk=ttyS0) console=serial;      |     |    - for (i=0; i<NR_VECTORS; i++) set_intr_gate(  |
                                                      |      (but default off in 2.6.20)                 |     |        i, empty_zero_page); // Stub all           |
                                                      |    - printk("Kernel command line: %s\n", cmdline)|     |    - set_intr_gate(0x20, hw_interrupt); // IRQ0   |
                                                      |    [2.6.20: Basic serial/vga; no fbcon early]    |     |      .. up to 0x2F (32 IRQs)                       |
                                                      |    [Rationale: Visible output starts]            |     |    - set_system_gate(0x80, sysenter_do_int80);    |
                                                      |                                                    |     |    - load_idt(); // lidt idt_descr (256*8=2KB)    |
                                                      |                                                    |     |    [2.6.20: Vectors 32-47=IRQs; no MSIx; TSS I/O  |
                                                      +---------------------------------------------------+     |     bitmap=0 (full access)]                        |
                                                                      |             +---------------------------------------------------+
                                                      +---------------------------------------------------+             |
                                                      |22. Softirq Init (softirq.c)                      |             v
                                                      |    (~300-310ms; [C]; Line ~50)                   |    +---------------------------------------------------+
                                                      |    - open_softirq(TASKLET_SOFTIRQ, tasklet_action)|    |23. RCU Init (rcu.c)                                 |
                                                      |    - open_softirq(HI_SOFTIRQ, raise_softirq);    |    |    (~310-320ms; [C]; Line ~100)                   |
                                                      |      .. (6 total: NET_TX=0, BLOCK=2, etc.)       |    |    - rcu_init_percpu_data(0, nr_cpu_ids);         |
                                                      |    - tasklet_init(..., do_softirq);              |    |    - register_cpu_notifier(&rcu_preempt_notifier);|
                                                      |    [2.6.20: No RCU boost; classic RCU only]      |    |    [2.6.20: Basic RCU; no tree-RCU variants;      |
                                                      +---------------------------------------------------+    |     grace period via jiffies]                      |
                                                                      |             +---------------------------------------------------+
                                                                      v                                           |
                                                      +---------------------------------------------------+             |
                                                      |24. Time Init (time.c)                            |             v
                                                      |    (~320-340ms; [C]; Line ~200-300)              |    +---------------------------------------------------+
                                                      |    - unsigned long tsc_khz = calibrate_tsc();    |    |25. Scheduler Init (sched.c)                         |
                                                      |      (loops=loops_per_jiffy * HZ / 1000)         |    |    (~340-380ms; [C]; Line ~1000-1200)              |
                                                      |    - pit_calibrate_tsc(lpj, tsc_khz, 50); // PIT |    |    void sched_init(void) {                         |
                                                      |      (if !tsc, use PIT 1193180 Hz)               |    |      prio_tree_init(); // For O(1) arrays          |
                                                      |    - xtime.tv_sec = 0; xtime.tv_nsec = 0;        |    |      for (prio=0; prio<140; prio++) init_array(   |
                                                      |    - jiffies = INITIAL_JIFFIES; // ~ -300s offset|    |        &active_array[prio], DEQUEUE_SAVE);        |
                                                      |    [2.6.20: HZ=250/1000; no highres timers;      |    |      init_idle(current, smp_processor_id());      |
                                                      |     lpj=loops_per_jiffy global]                  |    |        // PID 0, nice=0, policy=SCHED_NORMAL      |
                                                      +---------------------------------------------------+    |      init_bh(TIMER_BH, timer_bh); // Bottom halves|
                                                      |    [Rationale: Clock for scheduling; BogoMIPS print]   |    |    }                                               |
                                                                      |             [2.6.20: O(1) only; no CFS/EEVDF;      |
                                                                      v             140 prios (0-99 RT, 100-139 normal)]  |
                                                      +---------------------------------------------------+    +---------------------------------------------------+
                                                      |26. VFS Caches Init (init.c)                      |
                                                      |    (~380-390ms; [C]; Line ~400)                  |
                                                      |    - kmem_cache_create("dentry_cache", .., 0,    |
                                                      |      SLAB_RECLAIM_ACCOUNT); // ~512 entries      |
                                                      |    - kmem_cache_create("inode_cache", ..);       |
                                                      |      // ~256 inodes initial                      |
                                                      |    [2.6.20: No percpu caches; SLAB only]         |
                                                      +---------------------------------------------------+
                                                                      |                                           |
                                                                      v                                           |
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      |27. Proc Root Init (proc_root_init)               |     |28. rest_init() (main.c)                             |
                                                      |    (~390-400ms; [C]; proc.c Line ~50)            |     |    (~400-410ms; [C]; Line ~500)                    |
                                                      |    - proc_root = proc_mkdir("root", NULL);       |     |    int rest_init(void) {                           |
                                                      |    - proc_symlink("mounts", NULL, "self/mounts");|     |      kernel_thread(kthreadd, NULL, CLONE_FS |      |
                                                      |    - mount("proc", "/proc", "proc", MS_MNT_DIR,  |     |        CLONE_FILES); // PID 2, kthreadd_task      |
                                                      |        "");                                      |     |      kernel_thread(kernel_init, NULL, CLONE_FS |  |
                                                      |    [2.6.20: Basic /proc; no /proc/kallsyms early]|     |        CLONE_SIGHAND); // kernel_init thread       |
                                                      |    [Rationale: Debug FS for boot msgs]           |     |      local_irq_enable(); // sti (IRQs=1)           |
                                                      +---------------------------------------------------+     |      preempt_enable_no_resched();                  |
                                                                      |             |      do { } while (0); // Barrier      |
                                                                      |             |      schedule(NULL); // First sched;   |
                                                                      |             |        CPU0 -> cpu_idle() loop (hlt)   |
                                                                      |             |    }                                           |
                                                                      |             |    [Rationale: Multitask on; idle starts]|
                                                                      v             +---------------------------------------------------+
                                                      +---------------------------------------------------+             |
                                                      |29. Parallel: Idle Loop & Early IRQs              |             |
                                                      |    (Post rest_init; ~410ms+; [ASM/C])            |             v
                                                      |    - cpu_idle(): while (!need_resched()) {       |    +---------------------------------------------------+
                                                      |        default_idle(); // hlt; sti; cli loop     |    |30. kthreadd Daemon (kthread.c)                      |
                                                      |      }                                           |    |    (~410-420ms; [C]; Runs async)                   |
                                                      |    - IRQ0 (timer): do_timer_interrupt() ->       |    |    int kthreadd(void *unused) { for(;;) {         |
                                                      |      do_timer(1); jiffies++; xtime_update(1);    |    |      set_current_state(TASK_INTERRUPTIBLE);       |
                                                      |    [2.6.20: PIT IRQ0 @100Hz scaled to HZ; no APIC|    |      schedule(); // Wait for kthread_create calls  |
                                                      |     timer early]                                 |    |        e.g., later for keventd (/sbin/hotplug)    |
                                                      +---------------------------------------------------+    |    } }                                             |
                                                                      |             [2.6.20: Spawns workqueue threads;     |
                                                                      |             no kworkers]                          |
                                                                      |             +---------------------------------------------------+
                                                                      v                                           |
                                                      +---------------------------------------------------+             |
                                                      |31. kernel_init Thread Start (main.c)             |             |
                                                      |    (~420-430ms; [C]; Line ~550)                  |             v
                                                      |    int kernel_init(void * unused) {              |    +---------------------------------------------------+
                                                      |      create_proc_read_entry("cmdline", .., cmd); |    |32. do_basic_setup() (main.c)                        |
                                                      |      do_basic_setup(); // Drivers, FS            |    |    (~430-500ms; [C]; Line ~600-700)                |
                                                      |      prepare_namespace(); // Mount root          |    |    void do_basic_setup(void) {                     |
                                                      |      system_state = SYSTEM_RUNNING;              |    |      cpuset_init_smp(); // Early cgroups prep     |
                                                      |      proc_caches_init();                         |    |      usermodehelper_init(); // For /sbin/modprobe |
                                                      |      init_post(); // Exec init; exit(0);         |    |      bdi_init(&noop_backing_dev_info);             |
                                                      |    }                                             |    |      do_initcalls(); // All levels (see next)      |
                                                      |    [Rationale: Serial thread for user handover]  |    |    }                                               |
                                                      +---------------------------------------------------+    |    [2.6.20: No dm-crypt early; basic bdi]          |
                                                                      |             +---------------------------------------------------+
                                                                      |                                           |
                                                                      v                                           |
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      |33. Initcalls: Core/Postcore (do_initcalls)       |     |34. Initcalls: Arch/Subsys/FS (do_initcalls cont)   |
                                                      |    (~500-550ms; [C]; main.c Line ~300)           |     |    (~550-650ms; [C])                               |
                                                      |    static initcall_t *initcall_levels[] = { ... }|     |    - arch_initcall(smp_init); // If SMP, boot APs |
                                                      |    - for (level=0; level<7; level++)             |     |    - subsys_initcall(pci_init); // pci_scan_bus(0)|
                                                      |      for (call=levels[level]; call<end; call++)  |     |      -> enumerate devices (e.g., 00:1f.1 IDE)     |
                                                      |        if (!(*call)()) printk("call failed\n");  |     |    - fs_initcall(register_filesystem(&ext3_fs_type)|
                                                      |    [Level 0 core: page_launder_init()]           |     |      -> later mount uses it                        |
                                                      |    [Level 1 postcore: rcu_init_node()]           |     |    - device_initcall(ide_probe_module); // Disks  |
                                                      |    [2.6.20: ~250 calls; e.g., net_init (level2)] |     |    [2.6.20: Levels strict; no deferred probe]     |
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                                      |                                           |
                                                                      v                                           |
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      |35. Initcalls: Device/Late (do_initcalls end)     |     |36. prepare_namespace() (init.c)                     |
                                                      |    (~650-750ms; [C])                             |     |    (~750-850ms; [C]; Line ~800)                    |
                                                      |    - device_initcall(scsi_register()); // SCSI bus|     |    static noinline int prepare_namespace(void) {  |
                                                      |    - late_initcall(calibrate_delay); // Final lpj |     |      if (initrd_start) {                          |
                                                      |    - device_initcall(atkbd_setup_keyb); // PS/2  |     |        root_mountflags |= MS_RDONLY;              |
                                                      |    [Examples: rtc_init (hwclock), serial8250_init|     |        pivot_root("/initrd", "/initrd/initrd");   |
                                                      |     (ttyS0), usb_register() (OHCI/EHCI probe)]   |     |      }                                             |
                                                      |    [Rationale: Hardware enum; modular]           |     |      mount_block_root("/dev/root", "rootfstype"); |
                                                      |                                                    |     |        // e.g., mount("sda1", "ext3", MS_RDONLY)  |
                                                      +---------------------------------------------------+     |      mount("proc", "/proc", "proc", 0, "");        |
                                                                      |             |      mount("sysfs", "/sys", "sysfs", 0, "");    |
                                                                      |             |      mount("devtmpfs", "/dev", "devtmpfs", 0,"");|
                                                                      |             |    }                                               |
                                                                      v             |    [2.6.20: No overlayfs; ramfs for /dev early;    |
                                                      +---------------------------------------------------+     |     ERR: "VFS: Cannot open root device" panic]     |
                                                      |37. system_state = SYSTEM_RUNNING                 |     +---------------------------------------------------+
                                                      |    (In kernel_init; ~850ms; [C])                 |
                                                      |    - Set flag; wake_up(&system_wq); // Signals   |
                                                      |      ready threads                               |
                                                      |    [Rationale: Kernel steady; no more bootcalls] |
                                                      +---------------------------------------------------+
                                                                      |                                           |
                                                                      v                                           |
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      |38. init_post() (main.c)                          |     |39. Free Initmem & Mark RO (init_post)              |
                                                      |    (~850-860ms; [C]; Line ~900)                  |     |    (~860-870ms; [C]; Line ~950)                    |
                                                      |    void init_post(void) {                        |     |    - free_initmem(); // kmem_cache_free(__init    |
                                                      |      free_initmem();                             |     |      sections: text/data ~2-4MB reclaimed)        |
                                                      |      mark_rodata_ro();                           |     |    - mark_rodata_ro(); // set_memory_ro(__rodata) |
                                                      |      proc_sys_init(); // /proc/sys/mount         |     |      (CR0.WP protects; no PTE changes)             |
                                                      |      run_init_process("/sbin/init");             |     |    [2.6.20: Basic RO; no lockdown; slab frees]    |
                                                      |        // -> do_execve("/sbin/init", argv, envp) |     |    [Rationale: Security; reclaim RAM]             |
                                                      |    }                                             |     +---------------------------------------------------+
                                                      |    [Fallback chain: /etc/init -> /bin/sh -> panic|
                                                      |     "No init found. Try passing init= option"]  |
                                                      +---------------------------------------------------+             |
                                                                      |                                           |
                                                                      v                                           |
                                                      +---------------------------------------------------+     +---------------------------------------------------+
                                                      |40. do_execve("/sbin/init") (exec.c)              |     |41. ELF Load & User Space Entry (execve)            |
                                                      |    (~870-900ms; [C]; Line ~1000)                 |     |    (~900-950ms; [C]; binfmt_elf.c Line ~200)      |
                                                      |    - search_binary_handler(bprm); // ELF         |     |    - open_exec("/sbin/init"); // O_RDONLY|O_LARGE |
                                                      |    - bprm->argc=1; bprm->filename="/sbin/init";  |     |    - elf_map(vma): map text @0x8048000 (1.5MB),   |
                                                      |    - prepare_binprm(bprm); // Set argv[0]        |     |      data @0x804A000, stack @0xBFFFF000 (8MB)    |
                                                      |    - search_elf_loader(bprm);                    |     |    - set_regs(regs): eip=interp e_entry (0x80482E0)|
                                                      |    [2.6.20: SysV init ELF; no binfmt_script early|     |      esp=0xBFFFFE00 (argc,argv,envp)              |
                                                      |     for sh fallback]                             |     |    - current->mm->start_code=0x8048000;           |
                                                      +---------------------------------------------------+    |      dump_thread(regs, 0); // Set TS_POLLING       |
                                                                      |                                        |    [Rationale: Load PID1; switch to user CS=3]   |
                                                                      v                                        +---------------------------------------------------+
                                                      +---------------------------------------------------+             |
                                                      |42. PID 1 Fork/Context Switch                     |             |
                                                      |    (Post execve; ~950ms; [ASM/C] sched.c)        |             v
                                                      |    - sys_execve -> flush_old_exec(current);      |    +---------------------------------------------------+
                                                      |    - switch_to(init_task, prev); // ASM: pushf;  |    |43. /sbin/init Run (User Space)                     |
                                                      |      cli; movl %esp, TASK_RUNNING_STACK(%ebx);   |    |    (~950ms+; SysV init; /etc/inittab)              |
                                                      |      // Context to PID1; reparent orphans to 1   |    |    - getty on tty1; read /etc/inittab (id:1:init  |
                                                      |    - signals_enable(); // Unblock SIGCHLD etc.   |    |      default:3)                                    |
                                                      |    [2.6.20: No cgroups attach; basic signal setup|    |    - runlevel 3: /etc/rc3.d/S10sysklogd start      |
                                                      |     for init]                                    |    |      (udev, cron, sshd, ..)                        |
                                                      +---------------------------------------------------+    |    - Console: "Debian GNU/Linux 4.0 ... login:"   |
                                                                      |             [System Running: Kernel idle ~99%;    |
                                                                      |             user daemons up; full multiuser]      |
                                                                      v             +---------------------------------------------------+
                                                      +---------------------------------------------------+
                                                      |44. Parallel Steady State (Post-Handover)         |
                                                      |    (~950ms+; Ongoing)                            |
                                                      |    - kthreadd: Spawn pdflush, kswapd (mem mgmt)  |
                                                      |    - IRQ handlers: Keyboard (1), IDE (14), ..    |
                                                      |    - Scheduler: RR for RT; prio for normal       |
                                                      |    - VFS: /proc updates, sys_open calls          |
                                                      |    [2.6.20: No cgroups; O(1) runs ~95% idle;     |
                                                      |     jiffies tick every 4ms]                      |
                                                      +---------------------------------------------------+
