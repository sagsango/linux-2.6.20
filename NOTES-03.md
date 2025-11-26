Bootloader Handoff (GRUB Stage2/2 or LILO) 
[GRUB 0.97 or LILO 22.8 era for 2.6.20; no EFI/GRUB2; legacy BIOS INT19h chainload; 
 ~0-500ms total; real-mode 16-bit; no kernel globals/funcs yet; boot_params @0x90000 phys post-handoff]
    |
    +--> BIOS POST & MBR Load (Firmware -> Boot Sector) 
    [BIOS real-mode 16-bit; BDA/IVT setup; ~0-50ms; no structs; raw ports/INTs; 
     detects legacy hw (IDE=0x1F0/14 via INT13h)]
          |
          +--> Power-on Self-Test (POST): CPU reset to 0xFFFF0 IP (ROM entry); 
               RAM test (base 640KB via INT0x15 0x88); init PIC (0x20/0xA0), PIT (0x40), 
               DMA (0x00-0x0F); detect drives (INT0x13 AH=0x08)
               |
               [Data: BIOS Data Area (BDA) @0x400-0x4FF: u16 equip_flags[0x10]=0x01 
                (floppy present), u8 dma_ctrl[0x13]=0x00 (no chan alloc), 
                u16 mem_kb[0x13]=640; INT Vec Table (IVT) @0x0-0x3FF: vec0x13=0xEC59 
                (disk handler); no globals; raw I/O ports]
               |
               +--> INT 0x13 (Disk Services): Read MBR sector 0 (LBA=0, CHS 0:0:1) to 0x7C00; 
                    if CF=1 (error): "Non-system disk or disk error" via INT 0x10 AH=0x0E (teletype)
                   |
                   [Pseudo-ASM (BIOS ROM): mov ax,0x0201; mov cx,0x0001; mov dx,0x0080; 
                    mov es,0x0000; mov bx,0x7C



Real-Mode Magic Check & Entry (boot/setup.S) 
[arch/i386/boot/setup.S; ~1-5ms; [ASM]; Line ~1-50; real-mode 16-bit entry at 0x7C00 + 0x110; validates image from bootloader; jumps to start_of_setup if valid; no globals; raw boot header @0x1F1; 2.6.20: proto >=0x0205 for reloc; prints "Loading Linux..." via BIOS INT 0x10; state: PG=0 PE=0 IRQ=1 (pre-cli in header)]
    |
    +--> Entry at 0x7C00 + 0x110 (JMP Short Rel8 to start_of_setup) 
    [From bootloader far jump (ljmp $0x1000, $0x20); CS:IP reset to real-mode seg (CS=0x1000 for 0x100000 base); interrupts on (IF=1); no stack/seg change yet; boot_params @0x90000 phys]
          |
          +--> Short JMP Over Header Data: EB 0xF0 (rel -16 to 0x110 + 0xF0 = 0x200? wait, offset 0x110 is JMP to 0x967 start_of_setup ~line 229); NOP fill for align
          | |
          | +--> Header Skip: Bytes 0x00-0x1F0 = setup code/data (2KB); 0x1F1-0x21F = boot_header (proto 0x0205); 0x200+ = piggy.gz compressed
          | | [e.g., 0x1F1 u8 boot_flag = 0xAA55 (magic); u8 jump[2] = EB 0xF0 90 (JMP rel); u8 header_ver = 0x0205 (proto 2.05 for 2.6.20 reloc)]
          |
          +--> State Check: CMPW $0xAA55, 0x1FE (boot_flag in header); JE valid; else invalid (no hlt yet, but legacy check)
          | |
          | +--> Data Struct: struct setup_header {u8 boot_flag[2]; u8 header_ver; u16 type_of_loader; u32 flags; u16 setup_sects; u16 vid_mode; u16 root_flags; u32 aux_device; u16 setup_sects_high; u32 cmd_line_ptr; u32 initrd_start; u32 initrd_size; u8 setup_align; u8 reserved; u16 relocator_cmdline_size; u32 relocator_addr; u32 min_alignment; u32 xloadflags; u32 cmdline_size; u32 hardware_subarch; u64 hardware_subarch_data; u32 payload_offset; u32 payload_length; u64 setup_data; u32 pref_address; u32 init_size; u32 handover_offset; } embedded in boot_params @0x90000
          | | [2.6.20: version=0x0205; loadflags=0x204 (HEAP=0x80, HIGH=0x04, REL=0x200); cmdline_size=0 if no heap]
          |
          +--> Error if Invalid Magic: MOV $msg_invalid, %SI; CALL print_string (INT 0x10 AH=0x13 AL=0x01 BX=0x0007 CX=strlen ES=0x1000 BP=0x7C00 DI=0x0000); HLT; JMP $ (infinite loop)
          | |
          | +--> msg_invalid: "Invalid or corrupt kernel image" null-term @offset 0x7D00 in setup.S strings; [Rationale: Early validation; 2.6.20 adds proto check post-magic]
    |
    v
Early Print (Optional Banner) [Line ~10-20; "Loading Linux..." via BIOS INT 0x10; ~0.5ms; optional if debug, but default on in 2.6.20]
    |
    +--> MOV $msg_load, %SI; CALL puts (BIOS INT 0x10 AH=0x0E AL=char BH=0x00 BL=0x07 loop LODSB TEST AL,AL JZ ret INT 0x10 JMP loop)
    | |
    | +--> msg_load: "Loading Linux...\r\n" null-term @0x7D20; [e.g






Real-Mode BSS Zero & Stack Setup (header.S)
[arch/i386/boot/header.S; ~5-10ms; [ASM]; Line ~200-250; real-mode 16-bit; clears uninitialized data (BSS) and sets minimal stack for C calls; no globals; linker symbols __bss_start/__bss_stop; 2.6.20: Stack at 0x90000 (64KB downward); BSS ~1KB includes video vars; state: PG=0 PE=0 IRQ=1; post-magic check from phase 2]
    |
    +--> From start_of_setup (Line ~229; JMP to header entry after magic/proto check)
    [CS=0x1000 (seg for 0x100000 base); interrupts on; boot_params @0x90000; no stack yet; temp regs %SI=%DS=0x1000]
          |
          +--> Direction Flag Clear: CALL EXT(cld) (clear DF=0 for string ops like stosb; DF=1 would decrement %DI)
          | |
          | +--> Rationale: Ensure forward string moves for BSS zero/memset; [2.6.20: EXT=external label; inline asm in C or ASM call]
          |
          +--> Segment Reload (Safe Real-Mode): MOV %CS, %AX; MOV %AX, %DS; MOV %AX, %ES (consistent addressing; CS=0x1000 flat real-mode)
          | |
          | +--> [No FS/GS/SS change yet; SS/SP set next; Rationale: Real-mode flat; no GDT; segs 64KB lim]
          |
          +--> Stack Initialization: MOV $0x90000, %SP (stack ptr top, grows down 64KB to 0x88000 if overflow); MOV $0x90000, %SS (set SS seg)
          | |
          | +--> [64KB stack safe >BDA 0x500, <kernel 0x100000; %SP=0x90000; Rationale: Bridge to C main() calls; heap hint if CAN_USE_HEAP]
          | | [2.6.20: No per-cpu stack; single CPU real-mode; overflow? corrupt params (rare)]
          |
          +--> BSS Zeroing Loop: MOV $__bss_start, %DI (dest reg); MOV $0, %AL (fill value=0); MOVL $__bss_stop - __bss_start, %ECX (len/4 words); REP STOSB (zero bytes; ECX=total bytes, but movl for dwords? wait, stosb for bytes, ECX=bytes)
          | |
          | +--> Pseudo-ASM: mov %edi, %eax; mov %ecx, %edx; rep stosb %al, (%edi); [zero ~1KB BSS; %EDI advances; ECX decrements]
          | | [Linker syms: __bss_start=0x100200 (post-header data); __bss_stop=0x100800 (~1.5KB); includes video_mode, heap_top vars]
          | | [2.6.20: BSS in .bss section; no .data init (zeroed too if uninit); Rationale: Clean slate for C vars; prevents garbage in memmap/cmdline]
          |
          +--> Post-Zero Check (Optional; No Explicit in 2.6.20): TEST %ECX, %ECX; JZ done; else corrupt (rare, no hlt; assume ok)
          | |
          | +--> [Error: Partial zero? Undefined; bootloader corrupt image; 2.6.20: No check, trusts linker]
          |
          +--> Prep for C Call: PUSH $0 (argc dummy for main()); CALL main (boot/main.c entry; near call in real-mode; arg %SI or %BX=boot_params)
          | |
          | +--> [Rationale: Stack ready for C; main() returns to PM switch; total phase ~5-10ms; output: None (banner in phase 2)]
          |
          +--> 2.6.20 Specifics: BSS includes ramdisk_image, screen_info (from boot_params); no KASLR offset; setup_sects=4 fixed
          | |
          | +--> [Overall: Minimal bridge; next main.c copies E820/video; ERR: Stack overflow -> corrupt boot_params, PM fail]
    |
    v
Real-Mode Main Entry (Phase 4: boot/main.c main(struct boot_params *bp); copies params, probes video/initrd; ~10-50ms; calls go_to_protected_mode; state: PG=0 PE=0; C entry post-BSS/stack)






Real-Mode Param Copy & Heap Alloc (main.c)
[arch/i386/boot/main.c; ~10-15ms; [C]; Line ~100-150; real-mode C entry from header.S CALL main(struct boot_params *bp); copies boot_params to safe zero-page (0x90000); allocates heap if long cmdline; parses E820 map; no globals yet (boot_params is local); 2.6.20: boot protocol 2.05; heap for cmdline overflow >64KB (0x90000-0x10000); state: PG=0 PE=0 IRQ=1; bp in %SI or %BX from ASM; returns to ASM for PM switch]
    |
    +--> Main Entry: void main(struct boot_params *bp) [Line ~100; C entry; bp points to input params @0x90000 from bootloader]
          |
          +--> Input Validation: if (!bp || bp->hdr.boot_flag != 0xAA55) { /* rare, already checked in ASM */ return or hlt; } [No explicit in 2.6.20; assumes ASM validated]
          | |
          | +--> Data Struct: struct boot_params *bp = (struct boot_params *)0x90000; [~1024B; includes setup_header @offset 0x1F1; e820_map @0x2D0 (128 entries x20B=2.5KB); screen_info @0x0000; hdr.version=0x0205]
          | | [e.g., bp->hdr.type_of_loader=0x21 (GRUB); bp->hdr.loadflags=0x204 (HEAP=0x80, HIGH=0x04, REL=0x200); bp->cmd_line_ptr=0x10000]
          |
          +--> Param Copy to Zero-Page (Line ~105): memcpy(&boot_params, bp, sizeof(struct boot_params)); [boot_params is global? No, local copy to 0x90000 safe (input bp may be temp); ensures PM virt access]
          | |
          | +--> Pseudo-C: char *dest = (char *)0x90000; char *src = (char *)bp; for (int i=0; i<sizeof(boot_params); i++) dest[i] = src[i]; [~1024B copy; includes e820_map, screen_info, hdr]
          | | [Rationale: Persist boot_params for protected mode (phys=virt low); 2.6.20: Copy E820 to bp->e820_map (up to 128 entries; type=1 RAM,7 usable highmem)]
          | | [e.g., bp->e820_map[0] = {addr=0x0, size=0x9FC00, type=1}; bp->e820_entries=1 (updated in copy_e820map sub)]
          |
          +--> Heap Allocation (If Flag Set; Line ~110): if (boot_params.hdr.loadflags & CAN_USE_HEAP) { heap_top = malloc(0x1000); } // 4KB heap for overflow
          | |
          | +--> malloc(4KB): Simple allocator in real-mode (from free mem below kernel; 0x88000-0x90000 gaps); heap_top = 0x98000 (post-stack); heap_end = heap_top - 0x200 (guard)
          | | [Pseudo-C: static char *heap_top = 0x98000; if (loadflags & 0x80) { /* check free */ memset(heap_top, 0, 0x1000); } else heap_top = 0;]
          | | [Rationale: For cmdline >0x90000-0x10000=64KB (rare; long UUID root=); 2.6.20: CAN_USE_HEAP=0x80 in loadflags from GRUB; no free() needed (single use)]
          | | [e.g., if cmdline_size > 0x1000: cmd_line_ptr = heap_top; strcpy(heap_top, bp->cmd_line_ptr); bp->hdr.cmdline_size = strlen+1]
          |
          +--> E820 Memory Map Copy (Line ~115-125): copy_e820map(&bp->e820_map, boot_params.e820_map); [Sub-func: for(int i=0; i<bp->e820_entries; i++) boot_params.e820_map[i] = bp->e820_map[i];]
          | |
          | +--> Struct e820entry {u64 addr; u64 size; u32 type; u32 acpi_ext; } x128 max; [Copy 20B/entry; type=1=RAM,2=ACPI,3=reserved,4=high usable,5=bad; entries<=128]
          | | [e.g., boot_params.e820_map[0] = {addr=0x00000000ULL, size=0x0009FC00ULL, type=1, acpi_ext=0}; boot_params.e820_entries = bp->e820_entries (~10-20 from BIOS INT0x15 E820h)]
          | | [Rationale: Persist BIOS memmap for setup_arch(mem_init()); 2.6.20: No memmap= param; assumes <4GB non-PAE; highmem if size>896MB]
          | | [Error: Overflow entries>128: Truncate; printk "Too many E820 entries" later in kernel]
          |
          +--> Command Line Persist (Line ~130): if (boot_params.hdr.version >= 0x0202) { cmdline_ptr = 0x90000 + (heap_top ? heap_top - 0x90000 : 0); strcpy(cmdline_ptr, (char *)bp->cmd_line_ptr); boot_params.hdr.cmdline_size = strlen((char *)cmdline_ptr) + 1; } [Move cmdline to safe post-heap]
          | |
          | +--> Pseudo-C: char *cmd = (char *)boot_params.cmd_line_ptr; if (strlen(cmd) > 0x1000) { /* use heap */ cmd = heap_top; memcpy(cmd, boot_params.cmd_line_ptr, strlen); } boot_params.cmd_line_ptr = (u32)cmd;
          | | [e.g., cmdline "root=/dev/sda1 ro initrd=0x2000000 0x400000 quiet" ~50B; if >64KB (UUID+long), heap_top=0x98000; 2.6.20: Proto 2.02+ supports cmdline_size]
          | | [Rationale: Safe for PM (virt low); parse_early_param in setup_arch uses boot_command_line = (char *)boot_params.cmd_line_ptr]
          |
          +--> Return to ASM (Prep PM Switch; Line ~140): /* End main */ CALL go_to_protected_mode(bp); [Sub-func in pm.c; copies final bp, disables NMI/PIC, loads temp GDT/IDT]
          | |
          | +--> [Rationale: Prep for mode switch; total phase ~10-15ms; output: None (video probe next); 2.6.20: bp->hdr.version=0x0205 enables reloc in decompress_kernel]
          |
          +--> Error Path: Malloc fail (low mem <4KB free): heap_top=0; cmdline truncated; printk "Heap alloc failed" in kernel later (no panic in real-mode)
          | |
          | +--> [Overall: Bridge ASM-C; E820/cmdline persist for arch setup; no VFS/alloc; ERR: Copy corrupt -> PM GDT fail (triple fault)]
    |
    v
Video Mode Probe & Set (Phase 5: main.c after param copy; probes VESA/BIOS for console; ~15-20ms; sets bp->screen_info; 2.6.20: Default 80x25 text mode 3; supports 0xFFFF probe; next initrd load)







Real-Mode Video Mode Probe & Set (main.c)
[arch/i386/boot/main.c; ~15-20ms; [C]; Line ~200-250; real-mode C; probes BIOS/VESA for console mode after param copy; sets bp->screen_info for kernel console; no globals; uses BIOS INT 0x10; 2.6.20: Default 80x25 text mode 3; supports 0xFFFF=probe; fallback CGA mode 7; state: PG=0 PE=0 IRQ=1; post-phase 4 memcpy; next initrd load; output: Sets video mode visible on screen if changed]
    |
    +--> Orig Mode Check (Line ~200): if (bp->screen_info.orig_video_mode == 7) { /* CGA mode 7 (80x25 mono text); fallback to mode 3 color */ orig_video_mode = 0x03; } else if (orig_video_mode == 0xFFFF) { probe = 1; /* Auto-probe VESA/BIOS */ }
    | |
    | +--> Data Struct: struct screen_info in bp (offset 0x00, 64B): u8 orig_x=0, orig_y=0 (cursor pos); u16 orig_video_page=0 (VGA page); u16 orig_video_mode=0x03 (from BIOS 0x484); u8 orig_video_lines=25, orig_video_cols=80 (from 0x484-0x485); u8 orig_video_eax=0, orig_video_edx=0 (INT 0x10 returns); u8 orig_video_height=0, orig_video_width=0 (VESA); u8 orig_video_lfb=0 (linear FB); u8 orig_video_isVGA=1 (VGA present); u16 orig_video_ypanstep=0, orig_video_ypanposition=0 (pan); u16 orig_video_xpanstep=0, orig_video_xpanposition=0; u16 orig_video_v_idc=0 (vertical ID); u16 cl_magic=0xA33F (console lock), cl_magic_size=4; u8 cl_vesa=0 (VESA used?); u16 cl_vesa_width=0, cl_vesa_height=0, cl_vesa_bpp=0, cl_vesa_bank=0, cl_vesa_mode=0, cl_vesa_line_len=0, cl_vesa_fb_addr=0, cl_vesa_vesacolor=0, cl_vesa_vesasize=0, cl_vesa_hsync=0, cl_vesa_vsync=0, cl_vesa_lfb=0, cl_vesa_lfb_hi=0, cl_vesa_mode_hi=0, cl_vesa_refresh=0; [2.6.20: No cl_vesa_* extensions; basic orig_video_mode from BIOS]
    | | [e.g., BIOS INT 0x10 AH=0x12 BL=0x10: returns orig_video_isVGA=1 if VGA; mode=0x03 (80x25 color text default)]
    |
    +--> VESA/BIOS Probe (If probe=1; Line ~205-220): INT 0x10, AX=0x4F01 (VESA info func); if AX=0x004F (success): Parse VBE info @ES:DI (struct vbe_info {sig 'VBE2', version 2.0, modes ptr}); for (modes=0x101 to 0x11F; modes++) { get_vesa_mode(modes, desired=1024x768x16); if (ok) { vesa_mode = modes; break; } }
    | |
    | +--> get_vesa_mode(int mode, int width, int height, int depth): INT 0x10 AX=0x4F01 BX=mode (VESA mode info); if AX=0x004F: struct mode_info @ES:DI {winA_len=64KB, winB_len=0, gran=64KB, win_segA=0xA000, mem_model=6 (direct color), bits_per_pixel=16, bytes_per_scan=1024, xres=1024, yres=768}; if match (xres==width, yres==height, bpp==depth) return mode; else continue
    | | [Rationale: VESA 2.0+ for graphics modes; 2.6.20: Supports up to 0x11F (1280x1024x256); fallback if no VESA (old BIOS): mode=0x03; no FB (framebuffer) probe here (kernel fbcon later)]
    | | [e.g., successful: vesa_mode=0x118 (1024x768x64k RGB); bp->screen_info.cl_vesa_mode=0x118; cl_vesa_width=1024, cl_vesa_height=768, cl_vesa_bpp=16, cl_vesa_line_len=1024, cl_vesa_fb_addr=0xFD000000 (phys FB base)]
    | | [Error: INT 0x10 fail (AX!=0x004F): probe=0; fallback orig_video_mode=0x03; printk "VESA probe failed" in kernel]
    |
    +--> Mode Selection Logic (Line ~225): if (vesa_ok && vesa_mode) { vid_mode = vesa_mode; /* e.g., 0x118 */ } else if (orig_video_mode == 0xFFFF) { vid_mode = 0x03 (80x25 color text); } else vid_mode = orig_video_mode;
    | |
    | +--> [2.6.20: vga= param from cmdline overrides (parse in setup_arch); e.g., "vga=0x318" =0x0318=792 mode (1024x768x256); if vga=ask, interactive (not in boot); default probe=1 if 0xFFFF]
    | | [Rationale: Choose best console mode for kernel printk; text modes for early console (vga_con); graphics for fbcon if CONFIG_FB]
    |
    +--> Set Video Mode (Line ~230-240): outb(vid_mode >> 8, 0x3C2); /* Misc output reg high */ INT 0x10, AX=vid_mode (set mode func); if CF=1 error fallback
    | |
    | +--> Pseudo-C: unsigned short mode = vid_mode; outb(mode >> 8, 0x3C2); /* VGA misc reg for mode high */ asm("movw %0, %%ax; int $0x10" : : "r" (mode)); [AL=low, AH=high for mode; e.g., mode=0x118: AH=0x01, AL=0x18]
    | | [BIOS INT 0x10 AH=0x00 AL=mode (set video mode); for VESA AH=0x4F AL=0x02 BX=mode; success AX=0x004F; error CF=1 AH=err (e.g., 0x5F unsupported)]
    | | [e.g., set mode=0x03: 80x25 color text; screen clears; cursor home; for 0x118: VESA sets FB, resolution; 2.6.20: No pan/scroll set (orig_video_ypanstep=0)]
    | | [Rationale: Activate console for "Uncompressing..." print; 2.6.20: Fallback to orig if set fail; no error panic (probe only)]
    |
    +--> Update boot_params (Line ~245): bp->screen_info.orig_video_mode = vid_mode; bp->screen_info.orig_video_lines = 25; bp->screen_info.orig_video_cols = 80; /* Update for kernel use */
    | |
    | +--> [e.g., if VESA: bp->cl_vesa=1; bp->cl_vesa_width=1024; etc.; Rationale: Pass to early_printk/vga_con in start_kernel; 2.6.20: cl_magic=0xA33F set if console lock needed]
    |
    +--> Error Path: Set mode fail (CF=1, AH=0x5F unsupported): vid_mode = 0x03; retry set; printk "Video mode set failed, fallback to 80x25" in kernel (no real-mode print)
    | |
    | +--> [Overall: ~15-20ms (INT 0x10 ~1ms/call, loop 30 modes); output: Screen mode change visible (clear/resolution); 2.6.20: No EFI VBE (legacy BIOS only); next phase initrd load uses video for msg if fail]
    |
    v
Initrd Load & Decompress (Phase 6: main.c after video; loads initial RAM disk if cmdline "initrd="; ~20-30ms; gzip only; max 4MB; sets bp->initrd_start/size; state: PG=0 PE=0; next PM switch)








Real-Mode Initrd Load & Decompress (main.c)
[arch/i386/boot/main.c; ~20-30ms; [C]; Line ~300-350; real-mode C; loads initial RAM disk (initrd) if "initrd=" in cmdline; decompresses if gzipped; sets bp->hdr.ramdisk_image/size for kernel pivot_root; no globals (local vars); uses BIOS INT 0x13 for read; 2.6.20: Gzip only (no bz2/lzma); max 4MB size; state: PG=0 PE=0 IRQ=1; post-video probe phase 5; next PM switch in pm.c; output: "Loading ramdisk..." if debug, else silent; error: skip (no panic, kernel falls back to built-in root)]
    |
    +--> Initrd Param Parse (Line ~300): if (bp->hdr.type_of_loader != 0x21) return; // Assume GRUB=0x21 (LILO=0x41 skips? no, parse anyway); char *cmd = (char *)boot_params.cmd_line_ptr; char *initrd_str = strstr(cmd, "initrd="); if (!initrd_str) return; // No initrd param
    | |
    | +--> Sscanf Parse: sscanf(initrd_str + 7, "%lx %lx", &start, &size); // "%lx %lx" for hex start/end (e.g., "initrd=0x2000000 0x400000" -> start=0x2000000ULL, size=0x400000ULL); if (sscanf != 2) { printk("Invalid initrd param"); return; }
    | | [Rationale: Cmdline parse simple strstr/sscanf; 2.6.20: No UUID/loop support here (kernel mount_root parses); size in bytes; start phys high mem (>1MB kernel)]
    | | [e.g., cmdline "root=/dev/sda1 ro initrd=0x2000000 0x400000" ; initrd_str points to "initrd=0x2000000 0x400000"; +7 skips "initrd="; sscanf reads two %lx]
    | | [Error: Invalid format (no space or non-hex): sscanf=1 or 0; skip initrd; no printk in real-mode (debug only via video write? rare)]
    |
    +--> Size/Start Validation (Line ~305): if (size == 0 || start + size > 0x10000000ULL) { /* Too large or zero */ rd_size = -1; goto error; } rd_size = size; // Temp hold
    | |
    | +--> [2.6.20: Max implicit ~16MB safe (high mem <4GB); no explicit max 4MB here (kernel limit in prepare_namespace); start must be page-aligned? no, but kernel assumes phys]
    | | [Rationale: Prevent overflow (e.g., bogus cmdline); error rd_size=-1 -> skip; Rationale: Initrd for early rootfs (drivers like ext3.ko, ide-pci.ko for real root mount)]
    |
    +--> Load Ramdisk from Device (Line ~310): rd_size = load_ramdisk(unsigned long start, unsigned long size, unsigned long addr, int flags); // addr=0x2000000 (high safe, post-kernel 0x100000)
    | |
    | +--> load_ramdisk Sub-Func (internal; ~Line 350-400): if (type_of_loader != 0x21) return size; // GRUB only? no, LILO too; struct biosdisk_info *disk = get_disk_info(bp->root_dev); // From cmdline root= or BIOS DL=0x80
    | | [Pseudo-C: int sector_size = 512; int num_sectors = (size + sector_size - 1) / sector_size; for (int i=0; i<num_sectors; i++) { int lba = start / sector_size + i; int ret = biosdisk_read(disk, lba, 1, (char *)addr + i*sector_size); if (ret < 0) { printk("RAMDISK: load failed sector %d", i); rd_size = -1; break; } } return rd_size;]
    | | [biosdisk_read: INT 0x13 AH=0x42 (ext LBA) with DAP (packet: size=0x10, num=1, off=low16(addr), seg=high16(addr), LBA=lba low/high); CF=1 error AH=code (0x02 not found); fallback CHS calc if !LBA]
    | | [e.g., start=0x2000000, size=0x400000 (4MB) = 8192 sectors; read to 0x2000000; disk=root_dev=0x0803 (/dev/sda1, major8 minor3); INT 0x13 DL=0x80 for primary]
    | | [2.6.20: BIOS INT 0x13 only (no SCSI/USB); error codes: AH=0x01 invalid, 0x04 not found, 0x40 seek fail; rd_size = actual bytes loaded or -1]
    | | [Rationale: Load raw sectors to RAM (initrd as temp rootfs for modules); high addr 0x2000000 >kernel 0x100000 to avoid overwrite; no verify (assume good disk)]
    | | [Error: Read fail (CF=1): rd_size = - (num_failed_sectors * 512); if partial, "RAMDISK: Incomplete read" (video print via direct 0xB8000); skip full if rd_size < size]
    |
    +--> Decompress if Compressed (Line ~320): if (rd_size > 0 && flags & 0x01) { /* Compressed flag? 2.6.20: No explicit, assume gzip if size small */ gunzip((char *)addr, rd_size, (char *)addr); rd_size = gunzip_out_len; } // In-place decompress
    | |
    | +--> gunzip Sub-Func (internal gzip; ~Line 400-450): if (hdr[0] != 0x1F || hdr[1] != 0x8B) { /* Not gzip */ return rd_size; } inflate(gzip_stream, out_buf=addr, in_len=rd_size); rd_size = inflated_len; [Simple inflate loop (no zlib lib, hardcoded Huffman)]
    | | [Rationale: Initrd shipped gzipped to save space (~50% compression); in-place to same addr (assume room); 2.6.20: Gzip only (no bz2=0x02 or lzma; kernel decompress_kernel supports bz2/gzip)]
    | | [e.g., gzipped 2MB -> 4MB uncompressed ramfs (cpio archive with /dev, /proc, modules); hdr=0x1F8B at start; error: inflate fail (corrupt) -> rd_size=-1; "RAMDISK: gunzip failed"]
    | | [Error: Decompress fail (bad hdr or CRC): rd_size = -2; skip; no panic (kernel falls back to built-in /init)]
    |
    +--> Update boot_params (Line ~330): bp->hdr.ramdisk_image = addr; bp->hdr.ramdisk_size = rd_size; if (rd_size < 0) { /* Error */ video_write("RAMDISK: load failed\n", 0xB8000 + cursor); rd_size = 0; } // Clear on fail
    | |
    | +--> [e.g., bp->hdr.ramdisk_image=0x2000000; bp->hdr.ramdisk_size=0x400000; Rationale: Pass to kernel kernel_init/prepare_namespace for pivot_root(/initrd -> /dev/sda1); 2.6.20: No seal (kernel checks size>0)]
    | | [video_write: Direct VGA mem write (attr 0x07 white/black); cursor +=2; wrap at 80x25; optional debug]
    |
    +--> Return to Caller (Line ~340): return rd_size; // To main; if >0 success, else skip; CALL go_to_protected_mode(bp) next (pm.c)
    | |
    | +--> [Rationale: Optional temp rootfs for modular drivers (e.g., SCSI for root on USB); total ~20-30ms (INT 0x13 ~1ms/sector, 4MB=8K sectors ~8s worst, but buffered); 2.6.20: No multi-boot initrd (GRUB single file)]
    |
    +--> Error Path: Load fail (rd_size <0): "RAMDISK: Incomplete" via video_write (direct 0xB8000); bp->ramdisk_size=0; continue to PM (kernel no initrd); Decompress fail: Similar, rd_size=0; [No panic; kernel printk "No initrd" later]
    | |
    | +--> [Overall: Bridge bootloader to kernel rootfs; no alloc (raw memcpy); ERR: Disk error (bad sector) -> skip; 2.6.20: Max implicit (mem limit); gzip in-place assumes space (size*2 safe)]
    |
    v
Protected Mode Transition (Phase 7: boot/pm.c go_to_protected_mode(); ~30-40ms; disables NMI/PIC; loads temp GDT/IDT; CR0.PE=1; jumps to pmjump.S; 2.6.20: Temp IDT for #GP/#DF; next decompress phase 8)







Protected Mode Prep (go_to_protected_mode)
[boot/pm.c; ~30-40ms; [C]; Line ~50-100; real-mode C; prepares descriptors/interrupts for 32-bit protected mode switch; disables NMI/PIC; installs temp GDT/IDT; far jump to pmjump.S for CR0.PE=1; no globals (local structs); 2.6.20: Temp IDT for #GP/#DF exceptions; state: PG=0 PE=0 IRQ=1 (masks PIC); post-initrd phase 6; next pmjump.S mode switch; output: None (silent); error: Triple fault if GDT bad (reboot)]
    |
    +--> Entry: void go_to_protected_mode(struct boot_params *bp) [Line ~50; C entry from main.c CALL; bp @0x90000; real-mode 16-bit; no stack overflow check]
          |
          +--> IRQ & NMI Disable (Line ~55): mask_pic_irq(0xFFFF); // Mask all 8259A PIC IRQs (master/slave ports 0x21/0xA1 outb 0xFF)
          | |
          | +--> Sub-Func mask_pic_irq(u16 mask): outb(0xFF, 0x21); outb(0xFF, 0xA1); // Master mask all 8 IRQs (0-7), slave all (8-15); cascade IRQ2 unmasked but slave masked
          | | [Rationale: Prevent legacy IRQs during switch (e.g., timer IRQ0=0x20 vector); 2.6.20: Legacy PIC (no APIC early); mask=0xFFFF all bits 1=masked; IRQ14 IDE primary=0x0E (14) masked until request_irq in probe]
          | | [e.g., outb(0xFF, 0x21): bits 0-7=1 (mask IRQ0 timer,1 kbd,6 floppy,7 printer); outb(0xFF, 0xA1): bits 0-7=1 (mask IRQ8 RTC,9 sci,10 nic,11 nic,12 mouse,13 math copro,14 IDE sec,15 IDE pri)]
          | | [Error: No check; if PIC already masked, redundant; Rationale: Safe transition (no spurious IRQs in PM)]
          |
          +--> NMI Disable (Line ~60): outb(0x80, 0x70); // CMOS reg B port 0x70 AL=0x80 (bit7=1 disable NMI); outb(0x00, 0x71); // Data 0x00
          | |
          | +--> [Rationale: NMI (non-maskable IRQ) from parity errors/timer; disable to prevent during GDT load (NMI uses IDT); 2.6.20: No NMI handler early; re-enable in setup_arch if needed]
          | | [e.g., CMOS reg B bit7=0 enable NMI (default); set=1 disable; port 0x70 select reg, 0x71 write data; error: CMOS locked? Rare, ignore]
          |
          +--> Temp GDT Load (Line ~65): load_gdt(gdt); // lgdt [gdt_desc] (struct desc_ptr {u16 limit=0x18; u32 base=gdt_base=0x800}; GDT 24B: 3 entries)
          | |
          | +--> Sub-Func load_gdt(struct gdt_ptr *d): asm("lgdt %0" : : "m" (*d)); [Flush pipeline; GDT base 0x800 phys (temp flat); limit 0x18 (6B for 3 entries)]
          | | [GDT Entries: 0 null (0x00000000 limit 0xFFFFF base 0x00000000); 1 code (0x00009A00 limit 0xFFFFF base 0x00000000 DPL0 32b gran=1); 2 data (0x00009200 limit 0xFFFFF base 0x00000000 DPL0 32b gran=1); flat 4GB access]
          | | [Rationale: Temp GDT for PM flat model (code exec/rw, data rw); 2.6.20: No user seg yet (udata in uncompressed head.S); base 0x800 low mem safe]
          | | [Error: Invalid GDT (bad limit/base): #GP fault on lgdt (but real-mode no #GP; undefined crash)]
          |
          +--> Temp IDT Install (Line ~70): lidt idt_desc; // Empty IDT (limit=0x17F=384B for 48 entries; base=0x0000; all stubs to empty_zero_page)
          | |
          | +--> Sub-Func lidt: asm("lidt %0" : : "m" (idt_desc)); [idt_desc = {limit=0x17F, base=0x0000}; IDT entries 0-47: gate type=0x06 (INT 32b), offset=0x0000, sel=0x08 (code), DPL=0]
          | | [Rationale: Basic exceptions (#DE=0, #DB=1, #BP=3, #OF=4, #BR=5, #UD=6, #NM=7, #DF=8, #TS=10, #NP=11, #SS=12, #GP=13, #PF=14, #MF=16); 2.6.20: 48 entries (up to #MF); empty_zero_page=0x0000 (int 0x00 does nothing)]
          | | [e.g., IDT[13] = {offset_low=0x0000, sel=0x08, 0x00 (resvd), type_attr=0x06 (interrupt gate), offset_high=0x0000}; #GP on bad seg in PM]
          | | [Error: Bad IDT: #GP on lidt (rare); no full 256 yet (uncompressed head.S)]
          |
          +--> Far Jump to Code Seg (Line ~75): ljmp $0x08, $1f; // Load CS=0x08 (code seg from GDT[1]); flush prefetch queue
          | |
          | +--> Label 1f: MOV $0x10, %AX; MOV %AX, %DS; MOV %AX, %ES; MOV %AX, %FS; MOV %AX, %GS; MOV %AX, %SS; // Reload all data segs to GDT[2] flat 4GB rw
          | | [Rationale: Switch to PM segs (DPL0 kernel); SS last to avoid stack fault; 2.6.20: No TSS/TR load yet (head.S); flat model 0-4GB base/limit]
          | | [e.g., %DS=0x10 (data seg); all segs same for flat; error: Invalid sel 0x08/0x10 -> #GP post-jmp]
          |
          +--> Interrupts Enable (Masked; Line ~80): STI; // Set IF=1 (enable maskable IRQs); but PIC masked, so no IRQs yet
          | |
          | +--> [Rationale: Allow future STI in PM; NMI disabled; 2.6.20: No early IRQ handlers; timer/PIC re-init in setup_arch]
          | | [Error: STI during bad IDT: Spurious #GP if IRQ; but masked safe]
          |
          +--> Jump to Compressed Kernel (Line ~85): if (bp->hdr.code32_start) { jmp *bp->hdr.code32_start; } else { MOV $0x100000, %EAX; JMP *%EAX; } // To startup_32 in compressed head.S
          | |
          | +--> [Rationale: Handoff to decompress; code32_start=0 if default; 2.6.20: Reloc if loadflags & LOADED_HIGH (rare, default 0x100000); next phase decompress_kernel]
          | | [e.g., jmp 0x100000 (compressed misc.c); error: Bad addr -> seg fault (no #PF handler, triple fault reboot)]
          |
          +--> 2.6.20 Specifics: Temp GDT no PAE/CR4 (head.S); IDT base=0 (empty); no per-cpu; loadflags & CAN_RELLOAD adjusts code32_start if !0x100000
          | |
          | +--> [Overall: ~30-40ms (outb/lgdt ~1us each); silent; ERR: GDT/IDT bad -> triple fault (reboot loop); Rationale: Minimal PM env (flat segs, no paging); next ASM pmjump.S sets CR0.PE=1]
    |
    v
Mode Switch Assembly (Phase 8: boot/compressed/pmjump.S; ~40-50ms; [ASM]; atomic CR0.PE=1; reload segs; jmp to startup_32 compressed; 2.6.20: No PG enable (post-decompress); state: PE=1 PG=0 after; next decompress)








Mode Switch Assembly (pmjump.S)
[arch/i386/boot/compressed/pmjump.S; ~40-50ms; [ASM]; Line ~1-20; real-mode 16-bit ASM; atomic switch to 32-bit protected mode by setting CR0.PE=1; reloads segs after GDT load; jumps to startup_32 in compressed head; no globals/locals (inline); 2.6.20: No paging enable (CR0.PG=0 post-switch, head.S does it); state: Pre PE=0 PG=0 IRQ=1 (masked); post PE=1 PG=0 IRQ=1; from go_to_protected_mode CALL; next decompress_kernel in misc.c; output: None (silent); error: Triple fault if GDT bad (reboot loop via BIOS)]
    |
    +--> Entry Point: .code16 (16-bit real-mode ASM); protected_mode_jump: [Line ~1; far call from pm.c ljmp $0x08, $protected_mode_jump; but pm.c does ljmp $0x08, $1f to flush, then CALL this? Wait, pm.c ends with CALL protected_mode_jump (near in real-mode)]
          |
          +--> CLI Disable IRQs: CLI; // Clear IF=0 (disable maskable IRQs; NMI already disabled in pm.c); [Rationale: Prevent IRQ during CR0 change (e.g., timer NMI?); 2.6.20: PIC masked, but safe; no local_irq_save (real-mode no flags save)]
          | |
          | +--> [State: IF=0; no pushf (not needed for short); error: Spurious IRQ mid-switch -> undefined (no IDT handlers, #DF double fault)]
          |
          +--> CR0 Protected Enable: MOV %CR0, %EAX; OR $0x01, %EAX; MOV %EAX, %CR0; // Set PE bit1=1 (protected mode enable); no PG bit31 (0)
          | |
          | +--> Pseudo-ASM: mov %cr0, %eax; or $1, %eax; mov %eax, %cr0; [CR0 low bits: PE=1, MP=0 (no FPU), EM=0 (emulate FPU?), TS=0 (task switch FPU), ET=1 (387 FPU), NE=0 (no native err); 2.6.20: No WP=1 (head.S); PE=1 switches CPU to PM (seg desc lookup)]
          | | [Rationale: Atomic enable (no interrupts); PE=1 changes opcode fetch to use CS desc (GDT[1] code 32b flat); error: Bad CR0 (bits 4-0 resvd=0) -> #GP, but real-mode no #GP (crash)]
          | | [e.g., CR0 pre=0x60000010 (ET=1); post=0x60000011 (PE=1); flush TLB implicit on PE change]
          |
          +--> Pipeline Flush Far Jump: LJMP $0x08, $1f; // Load CS=0x08 (GDT[1] code seg DPL0 32b exec); far jmp flushes 16-bit pipeline for 32-bit code
          | |
          | +--> Label 1f: Now in PM 32-bit (CS=0x08); [Rationale: Far jmp reloads CS desc, invalidates prefetch; 2.6.20: No long mode (32b only); error: Invalid sel 0x08 -> #GP via temp IDT[13] (empty, #DF)]
          |
          +--> Segment Reload Post-PE (Line ~5-10): MOV $0x10, %AX; MOV %AX, %DS; MOV %AX, %ES; MOV %AX, %FS; MOV %AX, %GS; MOV %AX, %SS; // All data segs to GDT[2] 0x10 (flat 4GB rw DPL0)
          | |
          | +--> [Rationale: Reload segs to PM descs (base=0 limit=4GB gran=1); SS last to avoid stack seg fault mid-reload; FS/GS=0x10 (no per-cpu early); 2.6.20: Flat model (no user 0x23 yet, head.S adds); error: Invalid 0x10 -> #GP]
          | | [e.g., %DS=0x10 (data seg); all same for kernel flat; %ESP unchanged (stack phys low, SS flat ok)]
          |
          +--> STI Enable IRQs (Masked; Line ~12): STI; // Set IF=1 (enable maskable IRQs); PIC masked, so no effect yet
          | |
          | +--> [Rationale: Restore IF for PM (was CLI); NMI off; 2.6.20: No early handlers (IDT empty); re-mask if needed in setup_arch]
          | | [Error: STI with bad IDT: Spurious IRQ -> #GP via IDT[vec] (empty_zero_page=0x00 nop, but #UD undefined opcode if bad)]
          |
          +--> Jump to Compressed Kernel (Line ~15): MOV $0x100000, %EAX; JMP *%EAX; // Indirect jmp to startup_32 in compressed head_32.S (decompress entry)
          | |
          | +--> [Rationale: Handoff to decompress; if bp->hdr.code32_start !=0 (reloc), jmp that; 2.6.20: Default 0x100000 (compressed misc.c); error: Bad addr -> #PF (no handler, triple fault)]
          | | [e.g., jmp 0x100000 (startup_32: BSS zero, decompress piggy.o); state post-jmp: PE=1 PG=0 32b flat; next phase decompress prints "Uncompressing..."]
          |
          +--> 2.6.20 Specifics: No CR4.PAE (head.S); no WP (head.S CR0.WP=1); reloc stub if LOADED_HIGH (loadflags&0x04, kernel @0x1000000); no SMEP/SMAP (post-3.0)
          | |
          | +--> [Overall: ~40-50ms (mov/cr0/jmp ~1us each, but flush ~10ms?); silent; ERR: GDT bad -> #GP triple fault (BIOS reboot); Rationale: Atomic PE=1 (no partial state); next compressed head VGA print]
    |
    v
Compressed Kernel Decompress (Phase 9: boot/compressed/misc.c decompress_kernel(); ~50-150ms; [C]; unpacks piggy.o bz2/gzip in-place @0x100000; prints "Uncompressing... done."; jumps to uncompressed head.S; 2.6.20: Basic gunzip/bz2; reloc if !0x100000; state: PE=1 PG=0)






Compressed Kernel Decompress (boot/compressed/misc.c)
[arch/i386/boot/compressed/misc.c; ~50-150ms; [C]; decompress_kernel(); real-mode C (32-bit post-PM switch); unpacks piggy.o (gzipped/bzipped vmlinux.bin) in-place @0x100000; prints "Uncompressing Linux... done." to VGA; handles relocation if loaded high; jumps to uncompressed startup_32 in head.S; no globals (local vars); 2.6.20: Supports gzip/bz2 (no lzma); basic relocate stub; state: PE=1 PG=0 IRQ=1 (masked); from pmjump.S jmp *0x100000; next uncompressed head BSS zero; output: "Uncompressing Linux... done." on screen; error: Decompress fail -> "Kernel decompress failed" video print, hlt loop]
    |
    +--> Entry: void decompress_kernel(unsigned long load_addr) [Line ~50; C entry from pmjump.S jmp *load_addr=0x100000; 32-bit PM flat; stack from pm.c SS=0x10]
          |
          +--> VGA Print Start Message (Line ~55): write_to_vga("Uncompressing Linux..."); // Direct VGA text mem write @0xB8000 (80x25=4000 chars x2B=8KB)
          | |
          | +--> Sub-Func write_to_vga(const char *str): unsigned short *vga = (unsigned short *)0xB8000 + cursor_pos; int attr = 0x0700 (white on black); for (char *p = str; *p; p++) { *vga++ = (*p | attr); cursor_pos += 2; if (cursor_pos >= 80*25) cursor_pos = 0; } // No cursor update (INT 0x10 AH=0x02 later if needed)
          | | [Rationale: Early feedback pre-decompress (user sees progress); 2.6.20: Attr 0x07 (light gray fg, black bg); cursor_pos global? local static=0; wrap at end of screen (no scroll)]
          | | [e.g., str="Uncompressing Linux...\r\n"; writes chars 0x55 0x6E... with attr; visible if mode=3 text; ~30 chars, advances cursor ~60B]
          | | [Error: Bad VGA mem (mode not text): Garbage display; no check (assume mode 3 from phase 5)]
          |
          +--> Input Validation & Output Prep (Line ~60): if (load_addr != 0x100000 && !(boot_params.hdr.loadflags & LOADED_HIGH)) { /* Invalid load */ write_to_vga("Load addr invalid"); hlt; } output = load_addr; input = &input_data; output_len = 0; // input_data = piggy.o start (linker symbol __piggy_start)
          | |
          | +--> Data: const unsigned char *input_data = (unsigned char *)__piggy_start; unsigned long input_len = __piggy_size; // Linker: piggy.o = gzipped vmlinux.bin (~15MB uncompressed -> 3-5MB gzipped); output = 0x100000 (in-place overwrite compressed code)
          | | [Rationale: In-place decompress (overwrite self); 2.6.20: __piggy_start=0x100200 (post-header); __piggy_size ~3MB; LOADED_HIGH=0x04 if bootloader loaded @0x1000000 (rare, adjust output)]
          | | [e.g., if loadflags & 0x04: output += 0xF00000; /* High load adjustment */; error: Bad piggy (corrupt image) -> decompress fail below]
          |
          +--> Decompression Loop (Line ~65-120): if (input[0] == 0x1F && input[1] == 0x8B) { /* Gzip */ gunzip(input, input_len, output, &output_len); } else if (input[0] == 0x42 && input[1] == 0x5A) { /* Bz2 */ bunzip2(input, input_len, output, &output_len); } else { /* Plain or error */ write_to_vga("Kernel not compressed"); memcpy(output, input, input_len); output_len = input_len; }
          | |
          | +--> gunzip Sub-Func (internal; ~Line 200-400): struct gzip_hdr {u8 id1=0x1F, id2=0x8B, cm=8 (deflate), flags, mtime[4], xfl, os=3 (Unix); }; if (hdr.cm != 8) return -1; inflate_blocks (Huffman decode loop: LZ77 + Huffman; fixed/dynamic trees; window 32KB); crc32 check (hdr.flags & 0x01?); return output_len or -1 (bad CRC/corrupt)
          | | [Rationale: Hardcoded gzip inflate (no libz); deflate = LZ77 (sliding window dict) + Huffman codes; 2.6.20: Basic impl (no large window opt); ~50-100ms on 1GHz P4 for 3MB]
          | | [e.g., inflate: bitstream read (MSB first); lit/len/dist codes; match prev output; CRC32 final match hdr.crc; error: CRC fail -> -1]
          | |
          | +--> bunzip2 Sub-Func (internal; ~Line 400-600): struct bz2_hdr {u8 id[3]="BZh", level='1'-'9'; }; if (!match) return -1; bzip2_blocksort (Huffman + Burrows-Wheeler + RLE); inverse BW transform; CRC check; return output_len or -1
          | | [Rationale: Bzip2 for better compression (~2.5MB); 2.6.20: Full impl (MTF, inverse RLE); slower ~100ms; fallback gzip if no "BZh"]
          | | [e.g., bz2: 6 tables (symbols, dist, etc.); run-length encoded runs >3; error: Bad magic or CRC -> -1]
          | | [Error: Decompress fail (corrupt hdr/CRC): write_to_vga("Decompress failed"); output_len=0; hlt loop (rare, image corrupt)]
          |
          +--> Relocation Adjust (If Needed; Line ~125): if (boot_params.hdr.loadflags & LOADED_HIGH && load_addr != 0x100000) { relocate_kernel(output, output_len); } // Stub adjust symbols for high load
          | |
          | +--> relocate_kernel Sub-Func (~Line 150): unsigned long delta = load_addr - 0x100000; for (sym in symtab) sym->addr += delta; // Basic reloc (add delta to syms); no full KASLR (2.6.20 no random)
          | | [Rationale: If bootloader loaded high (loadflags 0x04), adjust for decompress in-place; 2.6.20: Rare (default low); symtab from linker (vmlinux.sym); error: Bad symtab -> ignore (symbols optional)]
          | | [e.g., delta=0xF00000 (15MB high); sym startup_32 += delta; next jmp adjusted]
          |
          +--> VGA Print Done Message (Line ~130): write_to_vga(" ... done.\nBootup debug messages will be printed..."); // Append to "Uncompressing" (cursor from start msg)
          | |
          | +--> [Rationale: Feedback end; "Bootup debug" for early printk; 2.6.20: Str ~40 chars; attr 0x07; total screen line 1 used]
          | | [Error: VGA full: Wrap or overwrite; no scroll (simple write)]
          |
          +--> Jump to Uncompressed Kernel (Line ~135): jump_to_uncompressed(output); // ASM: jmp *output (to startup_32 in head.S uncompressed)
          | |
          | +--> Sub-Func jump_to_uncompressed(unsigned long entry): asm("jmp *%0" : : "r" (entry)); [entry = output + 0x1000? No, startup_32 at 0x100000 post-decompress; flush pipeline]
          | | [Rationale: Handoff to uncompressed head (BSS zero, page tables); 2.6.20: entry=0x100000 (fixed); error: Bad entry -> #PF triple fault]
          | | [e.g., jmp 0x100000 (startup_32: cld; zero BSS; build pgd; enable PG); state post-jmp: PE=1 PG=0 -> PG=1 in head]
          |
          +--> 2.6.20 Specifics: Piggy.o = vmlinux.bin + init sections (~15MB); gzip/bz2 only (CONFIG_KERNEL_GZIP); no ZSTD/LZ4 (later kernels); reloc basic (no full KASLR, fixed offsets)
          | |
          | +--> [Overall: ~50-150ms (decompress ~100ms dominant); visible "Uncompressing... done."; ERR: Corrupt piggy -> CRC fail hlt; Rationale: Save image size (ship compressed); in-place efficient (no extra mem); next head.S paging]
    |
    v
Uncompressed Head Startup (Phase 10: arch/i386/kernel/head.S startup_32; ~60-70ms; [ASM]; zeros full BSS; loads final GDT/IDT; builds page tables; enables paging CR0.PG=1; calls start_kernel C; 2.6.20: 4MB large pages opt; state: PE=1 PG=0 -> PG=1)







Uncompressed Head Startup (head.S)
[arch/i386/kernel/head.S; ~60-70ms; [ASM]; Line ~50-100; 32-bit protected mode ASM post-decompress; zeros full kernel BSS (~8MB .bss + .data uninit); handles relocation if loaded high; no C calls yet (pure ASM); linker symbols __bss_start/__bss_stop; 2.6.20: Virt addr 0xC0100000 post-highmap; state: PE=1 PG=0 IRQ=1 (masked); from decompress_kernel jmp *0x100000 (startup_32 entry); next page table build; output: None (silent); error: Bad BSS bounds -> memset overflow (corrupt kernel mem, crash later)]
    |
    +--> Entry: startup_32 (unsigned long *boot_params) [Line ~50; ASM label; boot_params in %ESI from decompress? No, ASM stub passes via regs; 32-bit flat segs from pmjump.S; no stack change yet (SS=0x10 flat)]
          |
          +--> Direction Flag Clear: CLD; // Clear DF=0 for string ops (stosl forward); [Rationale: Ensure increment %EDI in memset; DF=1 decrement would corrupt; 2.6.20: Standard for all string ops in boot]
          | |
          | +--> [State: 32-bit PM; CS=0x08 code, DS/ES/SS=0x10 data flat 4GB; %ESP from pm.c (low phys); error: No check; DF garbage -> wrong zero dir]
          |
          +--> BSS Length Calc: MOVL $(__bss_stop - __bss_start), %ECX; // ECX = len in bytes (~8MB full kernel BSS post-decompress)
          | |
          | +--> Linker Syms: __bss_start = end of .data (e.g., 0xC0100000 virt highmap? Wait, phys low pre-PG); __bss_stop = end of .bss (uninit data); [2.6.20: ~8MB total (kernel text/data ~4MB + bss); ECX=0x800000 bytes; Rationale: Zero uninit globals (e.g., mem_map, task structs)]
          | | [e.g., __bss_start=0x100000 + text_len + data_len (~0x100000 + 0x200000 + 0x100000 = 0x400000 phys pre-reloc); error: Bad linker (ECX negative? Unsigned, wrap)]
          |
          +--> Zero Fill Prep: XORL %EAX, %EAX; // EAX=0 (fill value for dwords); REP; STOSL; // Zero loop: while (ECX >0) { *(%EDI) = %EAX (dword 0); %EDI +=4; %ECX--; }
          | |
          | +--> Pseudo-ASM: xorl %eax,%eax; rep; stosl %eax, (%edi); // ECX words (len/4); %EDI dest = __bss_start; advances to __bss_stop
          | | [Rationale: Efficient dword zero (4B/step); 2MB iterations (~2ms on 1GHz); overlaps compressed code (safe post-decompress in-place); 2.6.20: Includes .data uninit? No, .data linker init=0; bss only uninit]
          | | [e.g., %EDI=0x100000 + 0x300000 =0x400000 start; loop 2M times (8MB/4=2M); post %EDI=0x800000 end; error: Overflow (ECX too large) -> zero past kernel (corrupt piggy remnant, decompress fail next)]
          |
          +--> Relocation Adjust (If Loaded High; Line ~60): if (!(boot_params->hdr.loadflags & LOADED_HIGH)) { /* Default low */ } else { /* Stub */ unsigned long delta = 0x1000000 - 0x100000; /* High delta */ /* Adjust syms */ for (sym in reloc_table) sym.addr += delta; }
          | |
          | +--> Reloc Table: Linker reloc entries (vmlinux.reloc? No, 2.6.20 basic stub in misc.c relocate_kernel called pre; here ASM adjust if needed); [Rationale: For bootloader high load (rare); 2.6.20: No full KASLR (fixed offsets); delta=15MB for 16MB load]
          | | [e.g., if loadflags & 0x04: mov delta, %ebx; add %ebx, startup_32 label? Stub ASM add; error: Bad delta -> wrong jmp in head]
          |
          +--> Post-Zero Validation (No Explicit; Line ~70): TEST %ECX, %ECX; JZ done; // Implicit (if ECX!=0 partial, but no branch; assume linker correct)
          | |
          | +--> [Rationale: No check in 2.6.20 (trusts decompress output); error: Partial zero -> uninit vars (e.g., mem_map garbage, mem_init crash later)]
          |
          +--> Prep for Next (Page Tables; Line ~75): MOV $pg0, %EDI; // Next fill pgd @0x2000 (page dir); [Rationale: Chain to phase 11; total ~60-70ms (memset dominant ~20ms); 2.6.20: Phys low (no virt yet); no output]
          | |
          | +--> 2.6.20 Specifics: __bss_start post-piggy (decompressed vmlinux .bss); no .ctors init (C++ none); reloc only if HIGH flag (bootloader choice); no SMP early (head SMP in setup)
          | | [Overall: Clean kernel mem; ERR: Bad bounds -> memset past end (corrupt code, #SE seg err later); Rationale: Post-decompress (overwrite safe); next pg tables for PG=1]
    |
    v
Page Table Construction (Phase 11: head.S after BSS zero; ~70-100ms; [ASM]; builds initial PGD/PTEs (identity low, high kernel map); sets CR3; enables CR0.PG=1/WP=1; 2.6.20: 4MB large pages opt; state: PE=1 PG=0 -> PG=1; next GDT/IDT load)







Page Table Construction (head.S)
[arch/i386/kernel/head.S; ~70-100ms; [ASM]; Line ~150-250; 32-bit protected mode ASM post-BSS zero; builds initial page directory (PGD @0x2000) and page tables (PTEs for lowmem identity map, kernel highmap); uses 4KB pages or 4MB large pages opt; sets CR3 to PGD; no C calls; linker swapper_pg_dir; 2.6.20: Default 4MB pgsz (CONFIG_X86_PAE=n); state: PE=1 PG=0 IRQ=1 (masked); from startup_32 after BSS; next GDT/IDT load phase 12; output: None (silent); error: Bad PTE (invalid phys addr) -> #PF triple fault post-PG=1]
    |
    +--> Entry Prep: MOV $pg0, %EDI; MOV $0x1000, %ECX; XOR %EAX, %EAX; REP STOSL; // Zero PGD page dir @0x2000 (4KB=1024 entries x4B=4KB; ECX=1024 dwords)
          |
          +--> pg0 Label: .space 4096 (linker alloc 4KB at 0x2000 phys low safe); [Rationale: Clear PGD (swapper_pg_dir = pg0); 2.6.20: Phys addr (no virt yet); %EDI=0x2000 start, post=0x3000 end]
          | |
          | +--> Pseudo-ASM: mov $0x2000, %edi; mov $0x1000, %ecx; xor %eax, %eax; rep stosl %eax, (%edi); // Zero 1024 PDEs (each u32 PDE=0); error: Overflow %EDI past 0x3000 -> corrupt next mem (rare, linker bounds)]
          | | [e.g., PGD all 0 (no mapping); Rationale: Initial dir for identity map (phys=virt lowmem) + kernel high (0xC0000000 virt for text/data)]
          |
          +--> PDE Index Calc for High Kernel Map: MOV $0x10000000 >> 22, %EBX; // EBX = 0x40 (PDE idx for 0xC0000000 virt = 1GB high kernel; >>22 = PDE (10b dir + 10b table +12b offset -> PDE at virt>>22)]
          | |
          | +--> [Rationale: x86 32b paging: 10b PGD idx (0-1023), 10b PTE idx, 12b offset; 4MB PDE if PS=1 (large page); 2.6.20: Highmap for kernel @0xC0000000 (CONFIG_PAGE_OFFSET=0xC0000000); lowmap identity 0-8MB]
          | | [e.g., 0xC0000000 >>22 = 0x40 (PDE 64); EBX=0x40; error: Wrong shift -> bad PDE idx (0x400 out of 1024, zero PDE no map)]
          |
          +--> Lowmem Identity Map Fill: fill_low_pte_table(0, 0x1000, 0x83); // Fill PDE 0 with 1024 PTEs for 0-4MB identity (phys=virt); flags 0x83 = present(1) rw(1) user(0) pwt(0) pcd(0) accessed(0) dirty(0) psize(0) global(0)
          | |
          | +--> Sub-Label fill_low_pte_table(int pte_idx, int num_pte, int pte_flags): MOV $lowmem_pte, %EDI; // PTE table @0x3000 (4KB post-PGD); MOV $num_pte*4, %ECX; MOV $pte_flags, %EAX; REP STOSL; // Fill num_pte dwords with flags (PTE=flags | phys_addr<<12, but phys=0 for identity low)
          | | [Rationale: Identity map low 0-8MB (first 2 PDEs: PDE0=0-4MB, PDE1=4-8MB); 4KB pages (psize=0); 2.6.20: 0x83 rw/present/exec (user=0 kernel); low for BIOS calls post-PG]
          | | [e.g., lowmem_pte all 0x83 (PTE=0x00000083 for phys 0); PDE0 = 0x00000083 | (0x3000 <<12)? No, PDE points to PTE table phys: MOV $0x3000 >>12, %EAX; OR $0x83, %EAX; MOV %EAX, pg0 + 0*4; // PDE[0] = phys_pte_table | flags]
          | | [Error: Bad fill (ECX wrong) -> partial map ( #PF on unmapped code); 2.6.20: Opt CONFIG_X86_4MB_PAGES: psize=1 in PDE, no PTE table (PDE=phys | 0x83 large page)]
          |
          +--> Kernel Highmap Fill: fill_pte_table(0xC00, 0x1000, 0x83, 0x100000); // Fill PDE 0xC0 (high kernel 0xC0000000-0xC0400000 64MB? Wait, 0xC00 >>10? No, idx=0xC0 for 0xC0000000 >>22=0xC0? 0xC0000000 >>22 = 0x300 (wait, 3GB offset >>22 = 0x300); wait, for kernel text/data ~8MB at 0xC0100000 >>22 = 0x301)
          | |
          | +--> Sub-Label fill_pte_table(int pde_idx, int num_pte, int pte_flags, unsigned long phys_start): MOV $pg0, %EDI; ADD $pde_idx*4, %EDI; MOV $highmem_pte, %ESI; MOV $num_pte*4, %ECX; MOV $0, %EAX; loop: MOV %EAX, %EBX; SHL $12, %EBX; OR %EBX, %pte_flags; MOV %EBX, (%ESI, %EAX,4); INC %EAX; LOOP loop; MOV $highmem_pte >>12, %EAX; OR $0x83, %EAX; MOV %EAX, (%EDI); // PDE[pde_idx] = phys_pte_table >>12 | flags; fill PTEs phys_start | flags
          | | [Rationale: Highmap kernel text/data (0xC0100000 virt = phys 0x100000 >>12 in PTE); 8MB map (2 PDEs?); 2.6.20: phys_start=0x100000 (decompressed kernel); flags 0x83 rw/present/exec kernel]
          | | [e.g., pde_idx=0x301 (0xC0100000 >>22=0x301); highmem_pte @0x4000; PDE[0x301] = 0x4000 >>12 = 0x1 | 0x83 = 0x84; PTEs 0x00001000 (phys 0x100000 >>12 = 0x10 | 0x83 = 0x93 for first)]
          | | [Error: Bad phys_start (non-page) -> #PF on kernel code; 2.6.20: No NX bit (CR4.PGE=0); large pages if CONFIG_X86_4MB: psize=1 in PDE, PTE skip, PDE = phys | 0x83 (4MB page)]
          |
          +--> PAE Enable if >4GB (Line ~200): MOV %CR4, %EAX; TEST $ (1<<6), %EAX; JZ no_pae; OR $0x20, %EAX; MOV %EAX, %CR4; // If PAE feature (CPUID EDX bit6), set CR4.PAE=1 for 36b phys addr
          | |
          | +--> [Rationale: PAE for >4GB RAM (PDPT + 2-level PD/PT); 2.6.20: Opt CONFIG_X86_PAE=y; CR4=0x10 ET=1 pre; post 0x30 PAE=1; no PSE=bit4 large pages if PAE (separate); error: No CPUID early (assume no PAE)]
          | | [e.g., CPUID 0x80000001 EDX & (1<<6) for PAE; if set, 4-level paging (PDPT @CR3); 2.6.20: Default non-PAE 32b phys <4GB]
          |
          +--> Post-Fill Prep: // PGD ready @0x2000 (low PDE0=0x83 | pte_phys, high PDE0x301=0x83 | high_pte_phys); MOV $pg0 + 0x1000, %EAX; MOV %EAX, %CR3; // Load CR3 = PGD phys 0x2000 (no >>12, full phys)
          | |
          | +--> [Rationale: CR3 points to PGD; flush TLB implicit on load; 2.6.20: Phys low (0x2000); error: Bad CR3 (non-page) -> #PF on next instr]
          |
          +--> 2.6.20 Specifics: swapper_pg_dir = pg0 (global sym); no SMEP (CR4.SMEP=bit20 post-3.0); highmap 0xC0000000 for kernel (CONFIG_PAGE_OFFSET); opt 4MB pages (PDE psize=1, no PT fill)
          | |
          | +--> [Overall: ~70-100ms (fill loops ~50ms); silent; ERR: Bad map (PDE=0 on kernel) -> #PF triple fault; Rationale: Enable virt mem safe (>4GB RAM); identity low for BIOS post-PG; next enable PG]
    |
    v
GDT/IDT Load & Paging Enable (Phase 12: head.S after tables; ~100-120ms; [ASM]; loads final GDT/IDT; sets CR0.PG=1 WP=1; far jmp reload CS; calls start_kernel C; 2.6.20: Final GDT with TSS; IDT 256 vectors; state: PE=1 PG=0 -> PG=1 WP=1; next start_kernel)








GDT/IDT Load & Paging Enable (head.S)
[arch/i386/kernel/head.S; ~100-120ms; [ASM]; Line ~250-300; 32-bit protected mode ASM post-page tables; loads final GDT (with kernel/user segs + TSS for tasks); installs full IDT (256 vectors with early handlers); sets CR0.PG=1 (paging enable) and CR0.WP=1 (write-protect); far jmp to reload CS after PG=1; calls start_kernel C stub; no locals (inline regs); linker gdt_descr/idt_descr; 2.6.20: GDT includes TSS[5] for proc0; IDT all 256 (IRQ 0x20-0x2F masked); state: PE=1 PG=0 IRQ=1 (masked) -> PE=1 PG=1 WP=1 IRQ=1; from phase 11 CR3 load; next start_kernel phase 14; output: None (silent); error: Bad GDT/IDT -> #GP triple fault; bad CR0 -> #PF on code fetch]
    |
    +--> Final GDT Load (Line ~250): LGDT gdt_descr; // Load Global Descriptor Table Descriptor {u16 limit=0x3FFF (8KB-1 for 512 entries? Wait, standard 48B for 6 entries); u32 base=gdt (phys 0xC0000000 high or low? Phys low pre-PG)}
          |
          +--> gdt_descr Label: .word 0x17FF, 0x0; .long gdt; // Limit 0x17FF (6KB for full GDT? Standard 0x17 (24B for 6 entries); base = gdt_table phys @0xC0008000 virt highmap? Phys low 0x800 pre-PG
          | |
          | +--> GDT Table (6 Entries, 8B each, 48B total): Entry0 null (0x0000000000000000 limit 0x0000 base 0x00000000); Entry1 kcode (0x00CF9A000000FFFF DPL0 32b gran=1 base=0 limit=4GB); Entry2 kdata (0x00CF92000000FFFF DPL0 32b gran=1 rw); Entry3 unused (0x00CF9A000000FFFF DPL0 code?); Entry4 udata (0x00CFF2000000FFFF DPL3 32b gran=1 rw user); Entry5 TSS (0x0000890000000000 DPL0 32b limit for task state seg proc0); [Rationale: Flat kernel (kcode exec/rw, kdata rw); user data DPL3 for later execve; TSS for %TR task switch (init_task TSS at 0xC0300000)]
          | | [2.6.20: TSS limit=0xFFFF (64KB for stack/regs); base=TSS_phys; no LDT; error: Bad limit/base -> #GP on lgdt (temp IDT[13] empty -> #DF double fault -> triple fault reboot)]
          | | [e.g., kcode attr 0x9A (code exec read DPL0 present); gran=1 4KB pages 4GB limit; post-lgdt flush seg cache on next jmp]
          |
          +--> Early IDT Install (Line ~255): LIDT idt_descr; // Load Interrupt Descriptor Table Descriptor {u16 limit=0x7FF (2KB for 256 entries); u32 base=idt_table phys low 0xC0000000? Phys 0x0000 early? Full base idt (phys low pre-PG)}
          | |
          | +--> idt_descr Label: .word 0x7FF, 0x0; .long idt_table; // Limit 0x7FF (256*8B=2048B-1); base = idt_table @0xC000E000 virt? Phys low 0x0000 for early stubs
          | | [Rationale: Full 256 vectors (0-255); early only 48 in pm.c; 2.6.20: Vectors 0x00-0x1F exceptions (#DE=0 divide, #DB=1 debug, #NMI=2, #BP=3 breakpoint, #OF=4 overflow, #BR=5 bound range, #UD=6 invalid op, #NM=7 no math copro, #DF=8 double fault, #9 resvd, #TS=10 task switch, #NP=11 seg not present, #SS=12 stack seg, #GP=13 general protection, #PF=14 page fault, #15 resvd, #MF=16 math fault, #AC=17 alignment check, #MC=18 machine check, #19-31 resvd); 0x20-0x2F IRQs (timer=0x20, kbd=0x21, etc.); 0x80 sysenter; traps 0x30-0x3F; softirq 0x80; IDT[vec] = {offset_low, sel=0x08 kcode, 0x00 resvd, type=0x0E interrupt gate DPL0, offset_high}; offset = handler addr (e.g., divide_error for 0)}
          | | [e.g., set_intr_gate(0, divide_error); ASM: movw $0x08, 2(%EDI); movw $0x8E00, 4(%EDI); movl $divide_error, (%EDI); rol $16, 6(%EDI); // Gate: low offset, sel, P=1 DPL0 type=0x0E, high offset; %EDI = idt + vec*8]
          | | [2.6.20: Handlers in entry.S (divide_error: push $0; pushl $do_divide_error; jmp all_traps;); all_traps: common stub push regs, call do_trap; error: Bad offset (0x0) -> #UD undefined; load_idt asm("lidt %0" : : "m" (idt_descr))]
          | | [Rationale: Full exceptions/IRQs for C code (start_kernel); traps for syscalls; 2.6.20: No ftrace/early debug; TSS I/O bitmap full (no I/O protect)]
          |
          +--> CR3 Load PGD (Line ~260): MOV $pg0 + 0x1000, %EAX; MOV %EAX, %CR3; // CR3 = PGD phys addr 0x2000 + 0x1000? Wait, pg0=0x2000, +0x1000=0x3000? No, PGD at 0x2000; MOV $0x2000, %EAX; MOV %EAX, %CR3; // Load page dir phys (no >>12, full 32b phys)
          | |
          | +--> [Rationale: Point CR3 to PGD (swapper_pg_dir); flush TLB implicit; 2.6.20: Phys low 0x2000 (pre-PG safe); error: Non-page CR3 -> #PF on next TLB miss]
          | | [e.g., CR3=0x2000; PDEs/PTEs phys low; identity map ensures code mapped]
          |
          +--> Paging & WP Enable (Line ~265-270): MOV %CR0, %EAX; OR $0x80000000, %EAX; OR $0x00010000, %EAX; MOV %EAX, %CR0; // OR PG=bit31=1 (paging enable), WP=bit16=1 (write protect kernel pages); JMP 1f (flush pipeline)
          | |
          | +--> Pseudo-ASM: mov %cr0, %eax; or $0x80010000, %eax; mov %eax, %cr0; 1: // PG=1 enables virt addr (TLB use PGD); WP=1 prevents user write to kernel pages (even supv mode)
          | | [Rationale: PG=1 for virt mem (>4GB safe); WP=1 for kernel protect (user can't write kernel via supv); 2.6.20: No NE=bit5 native err (FPU), PE=1 already; post-OR CR0=0x80010011; error: PG=1 with bad map -> #PF on code (triple fault)]
          | | [e.g., pre CR0=0x60000011 (PE=1 ET=1); post 0x80010011 (PG=1 WP=1); flush via jmp (invalidate TLB entries)]
          |
          +--> Far JMP Reload CS (Line ~275): MOV $1f, %EAX; JMP *%EAX; // Far jmp to self (reload CS desc post-PG); 1f: // Now virt addr (CS highmap if relocated)
          | |
          | +--> [Rationale: Far jmp flushes pipeline/TLB after PG=1 (virt fetch); 2.6.20: CS=0x08 kcode highmap 0xC0000000 + offset; error: Bad virt CS base -> #PF triple fault]
          | | [e.g., jmp 0xC0100000 + offset (high kernel virt); identity low ensures low phys code ok if not relocated]
          |
          +--> 2.6.20 Specifics: GDT base phys low pre-PG (0x800); IDT full 256 (vs temp 48 in pm.c); TSS for init_task (proc0); no CR4.PGE global pages (bit7=0); opt PAE from phase 11
          | |
          | +--> [Overall: ~100-120ms (lgdt/lidt/cr0 ~1ms, jmp flush ~10ms); silent; ERR: Invalid desc/CR0 -> #GP/#PF triple fault (BIOS reset); Rationale: Final descs for C (start_kernel); virt mem safe; next start_kernel memcpy params]
    |
    v
start_kernel() Entry (Phase 14: init/main.c; ~130-140ms; [C]; copies phys params to virt; sets SYSTEM_BOOTING; calls setup_arch; 2.6.20: No early console default; state: PE=1 PG=1 WP=1 IRQ=1 masked)







start_kernel() Entry (init/main.c)
[init/main.c; ~130-140ms; [C]; Line ~50-100; 32-bit protected mode C entry from head.S ASM stub (initial_code = start_kernel); copies phys boot_params to virt-safe (via __va); sets system_state=SYSTEM_BOOTING; calls setup_arch (arch init); no locals (global boot_params); 2.6.20: No early console by default (CONFIG_EARLY_PRINTK=n); state: PE=1 PG=1 WP=1 IRQ=1 (masked until rest_init); post-phase 12 far jmp; next lock/banner phase 15; output: "Linux version 2.6.20..." printk (if console ready); error: Bad __va (high phys) -> memcpy corrupt (crash in setup_arch)]
    |
    +--> Entry: asmlinkage void start_kernel(void) [Line ~50; ASM stub from head.S CALL *initial_code (%esi=boot_params?); asmlinkage for regs in C; 32-bit flat virt mem (highmap kernel code)]
          |
          +--> Boot Params Phys-to-Virt Copy (Line ~55): char *from = __va(boot_params_phys); char *to = __va(0); memcpy(to, from, sizeof(struct boot_params)); // Copy zero-page phys 0x90000 to virt 0 (identity map safe)
          | |
          | +--> __va Macro: #define __va(x) ((void *)((unsigned long)(x)+PAGE_OFFSET)) [PAGE_OFFSET=0xC0000000; phys low 0x90000 -> virt 0xC0009000; but low identity map virt=phys for 0-8MB]
          | | [Rationale: Post-PG virt addr (phys low not direct); sizeof(boot_params)=~1024B; 2.6.20: boot_params_phys=0x90000 (global? local from ASM); copy e820_map/screen_info/hdr to virt-safe for setup_arch]
          | | [Pseudo-C: unsigned long phys_bp = 0x90000; from = (char *)phys_bp; to = (char *)0; for (int i=0; i<1024; i++) to[i] = from[i]; // Ensures cmd_line_ptr virt (str at 0x10000 phys=0xC00010000 virt? No, low identity virt=phys for cmd)]
          | | [e.g., from=0x90000 phys (map to virt 0x90000 identity); to=0 virt (PGD PDE0 identity); error: memcpy beyond (rare) -> corrupt lowmem (mem_init fail later)]
          |
          +--> System State Init (Line ~60): system_state = SYSTEM_BOOTING; // Global enum {SYSTEM_BOOTING, SYSTEM_RUNNING, SYSTEM_HALT, SYSTEM_POWER_OFF, SYSTEM_RESTART}; [Rationale: State machine for initcalls (e.g., do_basic_setup checks !=BOOTING); 2.6.20: No halt states used early]
          | |
          | +--> [Global: extern enum system_states system_state; in include/linux/kernel.h; set to BOOTING=0; used in lock_kernel (if RUNNING allow preemption); error: Wrong state -> initcalls skip (broken boot)]
          |
          +--> Early Print Banner (Line ~65): printk(KERN_INFO "Linux version 2.6.20.%s (%s.%s-%s%s%s) (%s) " UTS_RELEASE UTS_VERSION UTS_MACHINE, build info); // UTS_* macros from Makefile (e.g., "2.6.20-gentoo-r8 (gcc 4.1.1)")
          | |
          | +--> printk Setup: Early console? No (console_init later); direct to vga_con if mode set; [Rationale: Boot banner with version/build; 2.6.20: KERN_INFO=6 (normal); %s for UTS (uname -a equiv); optional dump_stack() for backtrace if DEBUG]
          | | [e.g., output "Linux version 2.6.20-gentoo-r8 (root@buildhost) (gcc version 4.1.1 (Gentoo 4.1.1 p1.0)) #1 SMP Mon Jan 15 18:45:23 UTC 2007 i686"; visible if VGA mode 3]
          | | [Error: No console -> silent; 2.6.20: No early_printk default (ttyS0 if param)]
          |
          +--> Lock Kernel (Line ~70): lock_kernel(); // Big kernel lock (BKL) for SMP safety; in 2.6.20 UP: #define lock_kernel() preempt_disable(); // Disable preemption (no resched)
          | |
          | +--> [Rationale: Protect early init from preemption (though no sched yet); 2.6.20: BKL simple (preempt_disable); global kernel_flag spinlock? No, inline; unlock_kernel() later]
          | | [e.g., preempt_disable: local_irq_save(flags); (but no, just need_resched=0); error: Already locked -> deadlock (rare early)]
          |
          +--> Parse Early Params (Line ~75): parse_early_param(); // Scan boot_command_line for early params (e.g., "noapic console=ttyS0"); calls __setup handlers
          | |
          | +--> Sub-Func parse_early_param: char *p = boot_command_line; while (*p) { char *q = p; while (*q != ' ' && *q) q++; *q = 0; early_param(p); p = q+1; } // Tokenize by space; early_param calls __setup("noapic=", noapic_setup)
          | | [Rationale: Early arch params before setup_arch; 2.6.20: __setup in arch/i386/kernel/setup.c (e.g., "noapic" -> no_apic=1); no full parse (full in setup_arch)]
          | | [e.g., param "noapic" -> noapic_setup("noapic", NULL); sets apic_phys=0; error: Unknown param -> ignore]
          |
          +--> Root Mount Flags (Line ~80): root_mountflags &= ~MS_RDONLY; if (strstr(boot_command_line, "ro")) root_mountflags |= MS_RDONLY; // From cmdline "ro" or "rw"
          | |
          | +--> [Global: unsigned long root_mountflags = MS_RDONLY; in init.h; Rationale: Default ro for safety; 2.6.20: mount_block_root uses flags; error: Bad strstr -> wrong fs mount (rw unsafe)]
          |
          +--> Early Time Calibrate (Line ~85): find_tsc_pit_calibrate(); // Early TSC/PIT loops_per_jiffy for time_init
          | |
          | +--> Sub-Func: calibrate_delay_direct(); // Loops 10ms PIT (INT 0x15 delay); sets loops_per_jiffy global; [Rationale: For time_init(jiffies); 2.6.20: No TSC early (setup_arch full); ~1ms loop]
          | | [e.g., lpj = 1e6 * HZ / 100; printk "Calibrating delay loop... %ld.%02ld BogoMIPS" later]
          |
          +--> Call Arch Setup (Line ~90): setup_arch(&boot_command_line); // arch/i386/kernel/setup.c; parse cmdline full, CPUID, mem zones, per-cpu
          | |
          | +--> [Rationale: Arch-specific init (next phase 16); passes &cmdline for parse; 2.6.20: Calls trap_init, mm_init early inside]
          | | [Error: setup_arch fail (bad CPUID) -> panic rare]
          |
          +--> 2.6.20 Specifics: No reserved_mem (memblock later); basic cmd parse (full in setup_arch); lock_kernel UP simple (no spinlock); banner UTS from Makefile (gcc version in str)
          | |
          | +--> [Overall: ~130-140ms (memcpy ~1ms, parse ~5ms, setup_arch dominant); output: Banner printk if console; ERR: Bad copy -> corrupt e820 (mem_init panic); Rationale: Safe virt params; state BOOTING for dep initcalls]
    |
    v
Lock & Banner Print (Phase 15: start_kernel cont; ~140-150ms; [C]; locks BKL; verbose loglevel; prints full banner w/build info; 2.6.20: KERN_INFO normal; optional dump_stack; state: PE=1 PG=1 WP=1 IRQ=1 masked)









Stack/TSS Setup & C Jump (head.S)
[arch/i386/kernel/head.S; ~120-130ms; [ASM]; Line ~300-350; 32-bit protected mode ASM post-paging enable; sets proc0 stack (init_task_union.thread.esp); loads TR with TSS seg for task switching/FPU; calls initial_code stub to start_kernel C; no locals (regs); linker init_task_union/init_uvm; 2.6.20: TSS basic (no per-CPU); stack @0xC0303FFC (8KB down from 0xC0304000); state: PE=1 PG=1 WP=1 IRQ=1 (masked); from phase 12 far jmp 1f; next start_kernel phase 14; output: None (silent); error: Bad TSS/TR -> #TS triple fault; bad stack -> #SS on call]
    |
    +--> Stack Bottom Guard Setup (Line ~300): MOVL $empty_zero_page - 4, %EAX; // EAX = guard page phys 0xC0300000 -4 = 0xC02FFFFC (end of empty_zero_page, 4KB zeroed for fault detect)
          |
          +--> empty_zero_page Label: .fill 4096,1,0; // Linker 4KB zero page for #PF guard (on stack overflow); [Rationale: Stack guard below bottom (detect overflow #PF); 2.6.20: Phys low mapped identity, virt high? Wait, init_task at high 0xC0300000]
          | |
          | +--> Pseudo-ASM: movl $0xC0300000 - 4, %eax; // 0xC02FFFFC (dword before zero page end); Rationale: %ESP points below bottom (overflow writes guard, #PF on access); error: Bad addr (non-page) -> #PF immediate]
          |
          +--> Stack Pointer & Seg Set: MOVL %EAX, %SS; MOVL $__STACK_END - 4, %ESP; // SS=0x10 kdata flat; %ESP = stack end 0xC0304000 -4 = 0xC0303FFC (8KB stack down to 0xC0300000 guard)
          | |
          | +--> __STACK_END Label: .long init_task_union + THREAD_SIZE; // THREAD_SIZE=0x4000 (16KB? Wait, 8KB for proc0 stack in 2.6.20); init_task_union at 0xC0300000 (global sym for PID0 task struct + stack)
          | | [Rationale: Proc0 (idle/PID0) stack in union (struct task_struct + char stack[THREAD_SIZE]); %ESP top -4 (align dword); 2.6.20: SS last to avoid #SS seg fault mid-set; stack high virt (kernel map)]
          | | [e.g., %ESP=0xC0303FFC; push/pop safe down to 0xC0300000; overflow -> write empty_zero_page -> #PF (guard page mapped RO? No, zeroed RW, but detect in do_page_fault later)]
          | | [Error: SS=0x10 invalid post-GDT -> #SS; bad %ESP (non 16B align) -> unaligned access #AC if CR0.AM=1 (not set)]
          |
          +--> TSS Load for Task Switching (Line ~310): LTR $0x30; // Load Task Register TR=0x30 (GDT[6]? Wait, TSS seg=0x28 GDT[5]? Standard TSS sel=0x20 or 0x28; 2.6.20 TSS at GDT[5] sel=0x28)
          | |
          | +--> Pseudo-ASM: mov $0x28, %ax; ltr %ax; // Load TR with TSS seg sel (GDT[5] TSS desc); [Rationale: TSS for task switch (far jmp task), FPU save/restore (TS bit); init_task TSS at 0xC0300000 (proc0)]
          | | [TSS Desc GDT[5]: 0x000089000000FFFF (type 0x89 avail TSS DPL0 busy=0 present); base=0xC0300000, limit=0x103F (16KB for struct task + I/O bitmap full=0xFFFF); 2.6.20: Basic TSS (no LDT, I/O bitmap=0 full access); %TR=0x28 post-load]
          | | [e.g., ltr loads TSS desc cache; error: Invalid sel 0x28 (GDT[5] not TSS) -> #TS task switch fault via IDT[10] (empty early -> #DF triple fault)]
          | | [Rationale: Enable task switch/FPU (CR0.TS=0 initial); 2.6.20: Single task early (no switch); TSS.esp0 = stack for ring0 (set in sched_init)]
          |
          +--> C Stub Jump (Line ~320): CALL *%CS:initial_code; // Far call? No, near CALL initial_code (stub to start_kernel); initial_code = start_kernel in init/main.c (linker sym)
          | |
          | +--> initial_code Label: .long start_kernel, 0x10; // Offset to start_kernel + seg? No, near call in flat; [Rationale: Bridge ASM to C (start_kernel asmlinkage); 2.6.20: Stub simple CALL *(initial_code) (%CS prefix for seg? Flat no need)]
          | | [Pseudo-ASM: call *initial_code; // Jumps to 0xC0100000 + offset start_kernel; passes boot_params in %ESI? ASM stub sets %ESI=boot_params virt]
          | | [e.g., start_kernel at 0xC0100120; call enters C with regs intact; error: Bad offset (corrupt linker) -> #PF on fetch triple fault]
          |
          +--> 2.6.20 Specifics: init_task_union = {struct thread_info {unsigned long flags; struct task_struct *task; mm_struct *mm; ... char stack[THREAD_SIZE]; }}; THREAD_SIZE=4096*2=8KB; TSS.io_bitmap_offset=0xE0 (full I/O allow); no per-CPU TSS (SMP later)
          | |
          | +--> [Overall: ~120-130ms (ltr/call ~1ms); silent; ERR: Bad TR/SS -> #TS/#SS triple fault; Rationale: Proc0 ready (PID0 idle/task0); stack for C calls (preempt safe); next start_kernel memcpy params]
    |
    v
start_kernel() Entry (Phase 14: init/main.c; ~130-140ms; [C]; copies phys params to virt; sets SYSTEM_BOOTING; calls setup_arch; 2.6.20: No early console default; state: PE=1 PG=1 WP=1 IRQ=1 masked)








Lock & Banner Print (start_kernel)
[init/main.c; ~140-150ms; [C]; Line ~100-150; 32-bit protected mode C in start_kernel cont; acquires big kernel lock (BKL) for SMP safety; sets console_loglevel verbose; prints boot banner with version/build info; optional dump_stack for debug; globals: console_loglevel, kernel_flag (BKL); 2.6.20: BKL simple UP preempt_disable (no spinlock); state: PE=1 PG=1 WP=1 IRQ=1 masked; post-phase 14 params copy; next parse_early_params phase 16; output: "Linux version 2.6.20..." on console (VGA/ser if ready); error: No console -> silent banner; BKL deadlock rare early]
    |
    +--> Big Kernel Lock Acquire (Line ~100): lock_kernel(); // Acquire BKL for early init protection (SMP races); in 2.6.20 UP: #define lock_kernel() do { preempt_disable(); } while (0); // Disable preemption (set need_resched=0, no resched until unlock)
          |
          +--> Inline Implementation: preempt_disable(); // asm("pushfl; popl %0; orl $0x400000, %0; pushl %0; popfl" : "=r" (flags) : "0" (flags)); wait, no: simple current->preempt_count++; if (preempt_count ==0) local_irq_save(flags); [Rationale: Prevent resched during dep init (e.g., setup_arch); 2.6.20: No lockdep (later); global kernel_flag? No, inline preempt_count in task_struct]
          | |
          | +--> [Rationale: BKL legacy (removed 3.9); protects global data (e.g., mem_map); UP simple (no spin); error: Already disabled -> count overflow (rare, no check early); next unlock_kernel in end start_kernel]
          | | [e.g., current = &init_task; init_task.preempt_count = 1; no resched; 2.6.20: No RT preemption; SMP stub (if CONFIG_SMP=y, spin_lock(&kernel_flag) but UP nop)]
          |
          +--> Console Loglevel Set (Line ~105): console_loglevel = 7; // Full verbose (0 silent, 7 all KERN_DEBUG+); default 4 (warnings); [Rationale: Early boot verbose for debug (printk KERN_INFO/ERR); 2.6.20: Global int console_loglevel in printk.c; set before banner]
          | |
          | +--> [Global: extern int console_loglevel; in kernel/printk.c; used in vprintk (if level <= loglevel print); Rationale: Capture all msgs for /proc/kmsg or dmesg; error: High level -> flood (but early few msgs)]
          | | [e.g., console_loglevel=7 (KERN_EMERG=0 to KERN_DEBUG=7 all); 2.6.20: No default 4 (hardcoded 7 early); next console_init registers vga_con]
          |
          +--> Boot Banner Print (Line ~110): printk(KERN_INFO "Linux version 2.6.20.%s (%s.%s-%s%s%s) (%s) " UTS_RELEASE UTS_VERSION UTS_MACHINE, ...); // Full uname -a equiv with build details
          | |
          | +--> UTS Macros: From include/linux/uts.h and Makefile: UTS_RELEASE="2.6.20"; UTS_VERSION=".gcc4.1.1"; UTS_MACHINE="i686"; build user/host/date; [Rationale: Identify kernel for debug (version, compiler, build env); KERN_INFO=6 (normal level)]
          | | [Pseudo-C: char buf[256]; snprintf(buf, sizeof(buf), "Linux version %s.%s (%s@%s) (%s %s %s) %s %s\n", UTS_RELEASE, UTS_VERSION, build_user, build_host, build_cc, build_flags, UTS_MACHINE, build_date, build_time); vprintk(buf, args);]
          | | [e.g., output "Linux version 2.6.20-gentoo-r8 (root@buildhost) (gcc version 4.1.1 (Gentoo 4.1.1 p1.0)) #1 SMP Mon Jan 15 18:45:23 UTC 2007 i686"; visible on VGA if mode 3; ser if ttyS0]
          | | [2.6.20: UTS from autoconf.h; optional CONFIG_LOCALVERSION="-gentoo-r8"; error: No console (pre-register) -> buffered, flushed in console_init]
          |
          +--> Optional Backtrace Dump (Line ~115): #ifdef CONFIG_DEBUG_STACK_USAGE dump_stack(); // If enabled, print current stack trace (simple show_trace for early)
          | |
          | +--> dump_stack: if (console_trylock()) { show_stack(NULL, NULL); console_unlock(); } // show_stack: walk_stackframe (print %EIP %ESP etc.); [Rationale: Debug early stack for crashes; 2.6.20: CONFIG_DEBUG_STACK_USAGE=n default (off); simple hex dump]
          | | [e.g., output "Call Trace: [<c0100120>] start_kernel+0x0/0x..."; error: No lock -> skip; rare early (no bugs yet)]
          |
          +--> 2.6.20 Specifics: BKL UP inline (no spinlock.h); console_loglevel=7 hardcoded (no param); banner UTS from .config (e.g., CONFIG_LOCALVERSION=""); no smp_processor_id early (setup_arch)
          | |
          | +--> [Overall: ~140-150ms (printk ~5ms, lock ~0); output: Banner on console; ERR: printk fail (no console) -> silent; Rationale: Verbose start; protect init; next parse_early_params for "noapic" etc.]
    |
    v
Parse Early Params (Phase 16: start_kernel cont; ~150-200ms; [C]; scans boot_command_line for early arch params; calls __setup handlers; sets root_mountflags; 2.6.20: Basic strstr; full parse in setup_arch)







setup_arch(&command_line) (setup.c)
[arch/i386/kernel/setup.c; ~150-200ms; [C]; Line ~200-300; protected mode C; arch-specific init called from start_kernel; parses cmdline full (strstr for params); sets root_mountflags from "ro"/"rw"; calibrates TSC/PIT early for lpj; no locals (globals like boot_cpu_data); 2.6.20: No reserved_mem (bootmem later); basic parse (no full __setup scan, early only); state: PE=1 PG=1 WP=1 IRQ=1 masked; post-phase 15 banner; next CPU detect phase 17; output: Printk cmdline "Kernel command line: root=/dev/sda1 ro ..."; error: Bad parse -> wrong root_dev (mount fail panic later)]
    |
    +--> Entry: void setup_arch(char **cmdline_p) [Line ~200; called from start_kernel; cmdline_p = &boot_command_line (global char *boot_command_line = (char *)boot_params.cmd_line_ptr;)]
          |
          +--> Cmdline Persist: *cmdline_p = boot_command_line; // Set caller's ptr to global cmdline str (from boot_params.cmd_line_ptr virt-safe post-copy)
          | |
          | +--> Global: char *boot_command_line; [in setup.c; points to virt cmdline str @0x10000 (low identity) or heap if overflow; e.g., "root=/dev/sda1 ro quiet initrd=0x2000000 0x400000"; Rationale: Share with parse_early_param and full parse in do_basic_setup]
          | | [2.6.20: Str null-term; strlen ~100B; error: Bad ptr (corrupt copy) -> strstr crash segfault #PF]
          |
          +--> Early Param Parse (Line ~205): parse_early_param(); // Tokenize and call early __setup handlers (arch-only params before full parse)
          | |
          | +--> Sub-Func parse_early_param (internal ~Line 50): char *p = boot_command_line; while (*p) { char *q = p; while (*q && *q != ' ') q++; if (*q) *q++ = 0; if (early_param(p) < 0) break; p = q; } // Tokenize by space; early_param calls registered __setup
          | | [Rationale: Early arch params (e.g., "noapic" before APIC init); 2.6.20: Handlers in setup.c like noapic_setup(char *str, int *add) { no_apic=1; printk("No APIC\n"); }; registered via __setup("noapic=", noapic_setup)]
          | | [e.g., param "noapic console=ttyS0 earlyprintk=vga"; tokens "noapic" -> noapic_setup sets apic_phys=0; "console=ttyS0" ignored early (full in do_basic_setup); error: Bad handler -> return -EINVAL break parse]
          | | [Global: extern struct obs_kernel_param __setup_start[], __setup_end[]; in params.c; early filters arch-only; 2.6.20: ~20 early params (noapic, nousb, acpi=off)]
          |
          +--> Root Mount Flags Adjust (Line ~210): root_mountflags &= ~MS_RDONLY; if (strstr(boot_command_line, "ro")) root_mountflags |= MS_RDONLY; else if (strstr(..., "rw")) root_mountflags &= ~MS_RDONLY; // Default ro, override from cmdline
          | |
          | +--> Global: unsigned long root_mountflags = MS_RDONLY; [in include/linux/fs.h; MS_RDONLY=0x1; Rationale: Safe default ro for rootfs mount; used in prepare_namespace mount_block_root]
          | | [e.g., cmdline "ro" -> |=0x1; "rw" -> &= ~0x1; 2.6.20: No quiet parse here (full in mount_root); error: strstr fail (no "ro") -> default ro]
          |
          +--> Early Time Calibration: find_tsc_pit_calibrate(); // Calibrate TSC (RDTSC) vs PIT for loops_per_jiffy (lpj); early before full time_init
          | |
          | +--> Sub-Func find_tsc_pit_calibrate (internal ~Line 400): unsigned long tsc_start = rdtsc(); calibrate_delay_loop(1000000, 1); unsigned long tsc_end = rdtsc(); lpj = (tsc_end - tsc_start) * HZ / 1000000; if (lpj < 100) lpj = calibrate_pit(lpj); // PIT fallback if bad TSC
          | | [Rationale: lpj for time_init(jiffies increment); 2.6.20: rdtsc asm("rdtsc" : "=A" (tsc)); calibrate_delay_loop loops fixed delay (PIT INT0x15); printk "Calibrating delay loop... %ld.%02ld BogoMIPS" in time_init]
          | | [e.g., lpj ~2e9 on 2GHz CPU (HZ=250, 8M loops/ jiffy); error: Bad TSC (non-monotonic) -> PIT only (1193180 Hz / HZ); global unsigned long loops_per_jiffy;]
          |
          +--> Other Early Calls (Line ~215-230): reserve_setup_data(); // Scan bp->setup_data for mem resvd (rare); smp_setup_processor_id(); // Early CPU id (0); find_smp_cpu_map(); // If SMP, map APs (but early, full smp_init later)
          | |
          | +--> reserve_setup_data: if (bp->hdr.setup_data) { struct setup_data *data = (void *)bp->hdr.setup_data; while (data) { reserve_range(data->addr, data->len); data = (void *)data->next; } } // EFI memmap etc. (2.6.20 basic, no EFI)
          | | [Rationale: Reserve BIOS/ACPI mem; 2.6.20: setup_data chain (u64 next/addr/len/type); global memmap resvd; error: Bad chain -> loop corrupt mem]
          | | [e.g., smp_setup_processor_id: boot_cpu_id=0; find_smp_cpu_map: if CONFIG_SMP=y scan MADT for APs (but early stub)]
          |
          +--> 2.6.20 Specifics: No memblock_reserve (bootmem later); parse_early_param arch-only (full do_basic_setup); root_flags global for mount; lpj global for sched/time; no KASLR (fixed)
          | |
          | +--> [Overall: ~150-200ms (parse ~20ms, calibrate ~50ms); output: "Kernel command line: ..." printk; ERR: Bad cmdline ptr -> strstr crash #PF; Rationale: Arch prep (CPU/mem/cmd); next CPUID phase 17]
    |
    v
CPU Feature Detect & CR4 Set (Phase 17: setup.c cont in setup_arch; ~200-220ms; [C]; detects CPU family/model/step via CPUID; sets CR4 features (PAE/PGE/FXSR); patches alternative code for vendor; 2.6.20: Supports SSE/SSE2 no SSSE3; global boot_cpu_data)







CPU Feature Detect & CR4 Set (setup.c)
[arch/i386/kernel/setup.c; ~200-220ms; [C]; Line ~300-350; protected mode C in setup_arch cont; detects CPU family/model/stepping via CPUID; checks features (PAE/SSE/FXSR/PGE); sets CR4 to enable (mov cr4); patches vendor-specific code (alternative_instructions); globals: boot_cpu_data (struct cpuinfo_x86), x86_capability (unsigned long[NCAPINTS=16]); 2.6.20: Supports SSE/SSE2 (EDX bit25/26), no SSSE3 (added 2.6.30); state: PE=1 PG=1 WP=1 IRQ=1 masked; post-phase 16 parse_early_param; next memory init phase 18; output: Printk "CPU: Intel Pentium 4..." if verbose; error: Bad CPUID (old CPU <Pentium) -> fallback generic, no SSE panic]
    |
    +--> CPU Identification (Line ~300): identify_cpu(&boot_cpu_data); // Detect vendor/family/model/step via CPUID leaf 0x0/0x80000000; sets boot_cpu_data fields
          |
          +--> Sub-Func identify_cpu(struct cpuinfo_x86 *c) (~Line 50-100 in cpu/common.c): unsigned int eax, ebx, ecx, edx; asm("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (0)); // Leaf 0: max leaf=EAX, vendor str EBX/EDX/ECX="GenuineIntel"
          | |
          | +--> Vendor Detect: if (ebx == 0x756E6547 && edx == 0x49656E69 && ecx == 0x6C65746E) c->x86_vendor = X86_VENDOR_INTEL; else if (...) c->x86_vendor = X86_VENDOR_AMD; // Str "GenuineIntel" or "AuthenticAMD"
          | | [Rationale: Vendor-specific patches (alternative_instructions); 2.6.20: Intel P4 common (family 15); global enum x86_vendor_id {X86_VENDOR_INTEL=0, AMD=1, ...}; error: Unknown vendor -> X86_VENDOR_UNKNOWN, no patch]
          | | [e.g., CPUID 0: EAX=0x0B (max leaf), EBX=0x756E6547 ('Genu i'), EDX=0x6C65746E ('ntel'), ECX=0x49656E69 ('eIni'); c->x86_vendor=0 Intel]
          |
          +--> Family/Model/Stepping (Line ~60 in identify_cpu): asm("cpuid" : "=a" (eax) : "a" (1)); c->x86 = (eax >> 8) & 0xF; c->x86_model = (eax >> 4) & 0xF; if (c->x86 == 0xF) c->x86 += (eax >> 20) & 0xFF; c->x86_stepping = eax & 0xF; // Family (bits 8-11 + ext 20-27), model (4-7 + ext 16-19), step (0-3)
          | |
          | +--> Extended: if (c->x86_vendor == X86_VENDOR_INTEL) { asm("cpuid" : "=a" (eax) : "a" (0x80000000)); if (eax >= 0x80000001) asm("cpuid" : "=a" (eax), "=d" (edx) : "a" (0x80000001)); c->x86 += (eax >> 20) & 0xFF; // Ext family for P4+
          | | [Rationale: CPUID leaf 1 basic (family/model/step); leaf 0x80000001 ext for AMD/Intel; 2.6.20: P4 family=15 (0xF), model=3 (D0 rev), stepping=4; global struct cpuinfo_x86 boot_cpu_data = {x86_vendor=0, x86=15, x86_model=3, x86_stepping=4, x86_cache_size=1024, x86_capability[0..15]=features bits}; error: CPUID fault (old 386 no support) -> c->x86=4 generic 486, no SSE]
          | | [e.g., leaf 1 EAX=0x00000F13 (step4 model3 family15); printk "CPU: Intel Pentium 4 (family: 0xF, model: 0x3, stepping: 0x4)" if verbose]
          |
          +--> Feature Bits Set (Line ~310): asm("cpuid" : "=a" (eax), "=c" (ecx), "=d" (edx) : "a" (1)); // Leaf 1 features: EDX SSE2=bit26, SSE=bit25, FXSR=bit24, MMX=bit23, PAE=bit6, PSE=bit4 (4MB pages), PGE=bit7 (global TLB)
          | |
          | +--> set_bit(X86_FEATURE_SSE2, c->x86_capability) if (edx & (1<<26)); // Macro set_bit(bit, addr) = addr[bit/32] |= 1<<(bit%32); [NCAPINTS=16, 512 bits for features]
          | | [Rationale: Capability bitmap for if (cpu_has_pae); 2.6.20: EDX bit6 PAE for >4GB, bit25 SSE for vector, bit26 SSE2 for double; bit24 FXSR for FPU save/restore; no AVX (later); global unsigned long x86_capability[NCAPINTS]; boot_cpu_data.x86_capability = c->x86_capability]
          | | [e.g., EDX=0x078BF9FF (SSE2=1 bit26, SSE=1 bit25, PAE=1 bit6, PSE=1 bit4, PGE=1 bit7); test_bit(X86_FEATURE_PAE, x86_capability) = (x86_capability[0] & (1<<6)) !=0; error: No CPUID -> defaults 0 (no features, FPU disable)]
          |
          +--> CR4 Features Enable (Line ~320): unsigned long cr4 = mmu_cr4_features(); // Global u32 mmu_cr4_features = (1<<4) PSE | (1<<7) PGE | (1<<24) FXSR if features; if (cpu_has_pae) cr4 |= (1<<5) PAE;
          | |
          | +--> MOV %CR4, %EAX; MOV %EAX, %CR4; // Load/store CR4 (if CR4 exists, Pentium+); [Rationale: Enable paging ext (PGE global TLB no flush on seg change, PSE 4MB pages, FXSR fast FPU save, PAE 36b phys); 2.6.20: CR4 default 0x10 ET=1; post 0x10 | PSE=0x10 (bit4) | PGE=0x80 (bit7) | FXSR=0x10000 (bit24) | PAE=0x20 (bit5 if PAE)]
          | | [Global: extern u32 mmu_cr4_features; in mm/init.c; cpu_has_pae = test_bit(X86_FEATURE_PAE, boot_cpu_data.x86_capability); error: CR4 #UD (486 no CR4) -> fallback no ext (slow TLB)]
          | | [e.g., CR4=0x00000010 pre (ET=1); post 0x000100D0 (PAE=1, PSE=1, PGE=1, FXSR=1); printk "Enabling %s %s..." if debug]
          |
          +--> Vendor Alternative Patches (Line ~330): alternative_instructions(); // Patch code for vendor (Intel/AMD opt); calls alternative(mcount, mcount_64, X86_FEATURE_REP_GOOD); etc.
          | |
          | +--> Sub-Func alternative_instructions (internal ~Line 1000): if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) { /* Intel opt */ text_poke(...); } else if (AMD) { /* AMD opt */ }; // Patch jump tables for features (e.g., rep movs opt if REP_GOOD)
          | | [Rationale: Vendor-specific asm opt (faster memcpy if SSE); 2.6.20: Patches mcount (profiling), cmpxchg8b, etc.; global alternatives array in alternative.c; error: Bad patch -> wrong opt (slow code, no crash)]
          | | [e.g., if SSE2: patch rep movsb to SSE; printk "CPU: Using SSE2 for memcpy"; 2.6.20: No AVX; supports MMX/SSE opt]
          |
          +--> 2.6.20 Specifics: boot_cpu_data global struct cpuinfo_x86 {int x86, x86_vendor, x86_model, x86_stepping, x86_cache_size; unsigned long x86_capability[NCAPINTS]; char x86_vendor_id[16]; char x86_model_id[64]; ...}; identify_cpu sets all; no SSSE3 detect (CPUID leaf1 ECX bit0=1 SSSE3, but 2.6.20 no bit); CR4 no OSFXSR bit9 (FPU OS support later)
          | |
          | +--> [Overall: ~200-220ms (CPUID ~1us/leaf x10=10us, cr4 ~1ms, alt patch ~10ms); output: "CPU: Intel..." printk if KERN_INFO; ERR: No features -> fallback generic (no SSE panic, disable in /proc/cpuinfo); Rationale: Enable CPU ext for mm/smp (PAE for highmem, SSE for memcpy); next mem init phase 18]
    |
    v
Memory Init (Phase 18: setup.c mem_init; ~220-250ms; [C]; init bootmem allocator; build zonelists (DMA/Normal); alloc mem_map; set page counts; 2.6.20: No memblock; bootmem for early kmalloc)









Memory Init (setup.c mem_init)
[arch/i386/kernel/setup.c; ~220-250ms; [C]; Line ~400-500; protected mode C; initializes bootmem allocator from E820 map; builds zonelists (DMA/Normal/HighMem); allocates mem_map array of struct page for lowmem PFNs; sets initial page counts (free=1 RAM, 0 reserved); globals: mem_map (struct page *), max_low_pfn (unsigned long), zone_table[MAX_NUMNODES][MAX_NR_ZONES] (struct zone **), contig_page_data (struct pglist_data); 2.6.20: No memblock_reserve (bootmem only for early alloc); state: PE=1 PG=1 WP=1 IRQ=1 masked; post-phase 17 CR4 set; next per-CPU phase 19; output: printk "Memory: %luk/%luk available (%luk kernel code, %luk reserved, %luk data, %luk bss)"; error: Bootmem alloc fail -> panic("Out of memory")]
    |
    +--> Bootmem Allocator Init (Line ~400): bootmem_init(); // Scan E820 RAM regions; free_bootmem(start, size) for usable lowmem to bootmem pool
          |
          +--> Sub-Func bootmem_init (mm/bootmem.c Line ~100-150): unsigned long start_pfn, end_pfn; for (int i=0; i<boot_params.e820_entries; i++) { struct e820entry *desc = &boot_params.e820_map[i]; if (desc->type != E820_RAM) continue; start_pfn = desc->addr >> PAGE_SHIFT; end_pfn = (desc->addr + desc->size) >> PAGE_SHIFT; if (end_pfn > max_pfn) end_pfn = max_pfn; if (start_pfn >= end_pfn) continue; if (start_pfn < max_low_pfn) { if (end_pfn > max_low_pfn) end_pfn = max_low_pfn; free_bootmem(PFN_PHYS(start_pfn), PFN_PHYS(end_pfn - start_pfn)); } } // Free only lowmem RAM to bootmem
          | |
          | +--> Global: struct bootmem_data *bdata = &contig_page_data; [in mm/page_alloc.c; bdata->node_bootstart = 0; bdata->node_low_pfn = max_low_pfn; bdata->node_bootmem_map = mem_map_boot; (bitmap for free pages)]
          | | [Rationale: Bootmem pool for early kmalloc (pre-slab); free_all_bootmem later after slab; 2.6.20: Single node (MAX_NUMNODES=1); PFN_PHYS(pfn) = pfn << PAGE_SHIFT (4KB); error: No RAM regions -> bdata empty, alloc_bootmem fail panic]
          | | [e.g., e820 RAM 0x100000-0x3F000000: start_pfn=0x1000 (1MB/4KB), end_pfn=0x3F000 (1GB); free_bootmem(0x100000, 0x3EF00000); bdata->last_end_off = end_pfn; printk "bootmem init: free %luk" in debug]
          |
          +--> Max Low PFN Calc (Line ~405): max_low_pfn = max_low_pfn_mapped << (PAGE_SHIFT - 12); // From e820 max phys low / PAGE_SIZE; PAGE_SHIFT=12 (4KB); max_low_pfn_mapped = e820 max low phys >>12
          | |
          | +--> Global: unsigned long max_low_pfn = 0; unsigned long max_pfn = 0; [in mm.h; max_pfn = max(max_low_pfn, high_pfn from e820 highmem); Rationale: Lowmem <896MB (or 4GB non-highmem); 2.6.20: Highmem split if CONFIG_HIGHMEM=y >896MB; error: Bad e820 (negative PFN) -> 0, mem_init abort]
          | | [e.g., e820 low max 0x3F000000 (1GB) >>12 = 0x3F000 PFN; max_low_pfn=0x3F000; printk "lowmem end %lu" in mem_init; 2.6.20: No DMA32 zone (added later)]
          |
          +--> Zonelist Build (Line ~410): build_all_zonelists(NULL); // Link zones DMA(0-16MB pfn 0-0x4000), Normal(16MB-max_low_pfn), HighMem (if >896MB); NULL= no pgdat (contig single node)
          | |
          | +--> Sub-Func build_all_zonelists (mm/page_alloc.c Line ~2000-2100): for (int nid=0; nid < MAX_NUMNODES; nid++) { struct pglist_data *pgdat = NODE_DATA(nid); for (int zid=0; zid < MAX_NR_ZONES; zid++) { zone_table[nid][zid] = &pgdat->node_zones[zid]; } build_zonelists(pgdat); } // For each node/zone, link fallback lists (DMA -> Normal -> HighMem)
          | | [Rationale: Zonelists for __alloc_pages (GFP_DMA low only); 2.6.20: MAX_NUMNODES=1 UP, MAX_NR_ZONES=3 (ZONE_DMA=0 <16MB, ZONE_NORMAL=1 <896MB, ZONE_HIGHMEM=2); contig_page_data single pgdat]
          | | [Global: struct zone *zone_table[MAX_NUMNODES * MAX_NR_ZONES]; struct pglist_data contig_page_data = {node_zones[3], node_zonelists, ...}; zone_table[0][0] = &contig_page_data.node_zones[ZONE_DMA];]
          | | [e.g., dma_zone = &contig_page_data.node_zones[0]; dma_zone->zone_start_pfn = 0; dma_zone->spanned_pages = 0x4000; normal_zone->zone_start_pfn = 0x4000; highmem_zone if HIGHMEM=y start=max_low_pfn; printk "Built 1 zonelists in Zone order, mobility grouping on"; error: Bad zones -> NULL zone alloc fail #PF]
          |
          +--> Mem Map Array Alloc (Line ~420): mem_map = alloc_bootmem_pages(PFN_PHYS(max_low_pfn) / sizeof(struct page)); // Bootmem alloc for mem_map array covering lowmem PFNs
          | |
          | +--> Sub-Func alloc_bootmem_pages (mm/bootmem.c Line ~300-350): unsigned long size = PFN_PHYS(max_low_pfn) / PAGE_SIZE * sizeof(struct page); // Wait, / sizeof? No, size = max_low_pfn * sizeof(struct page); unsigned long addr = __alloc_bootmem_node(NODE_DATA(0), size, PAGE_SIZE, 0); if (!addr) panic("Out of memory"); return (void *)addr;
          | | [Rationale: mem_map[pfn] = struct page for lowmem (highmem separate); sizeof(struct page)=20B (32b flags, 16b _count, 32b private, 32b lru.next/prev ptrs, etc.); 2.6.20: Bootmem aligned PAGE_SIZE; global struct page *mem_map = NULL; error: No free bootmem -> panic("Out of memory", no sync)]
          | | [e.g., max_low_pfn=0x3F000 (1GB), size=0x3F000 * 20 = ~1.26MB; addr=0x1000000 (free high lowmem); mem_map = (struct page *)0x1000000; printk "mem_map at %p" if debug]
          |
          +--> Page Count Init Loop (Line ~430): for (unsigned long pfn = 0; pfn < max_low_pfn; pfn++) { struct page *page = mem_map + pfn; if (page_to_pfn(page) >= max_low_pfn) break; if (e820_pfn_ok(pfn)) set_page_count(page, 1); else set_page_count(page, 0); } // Mark RAM PFNs free count=1, reserved/non-RAM=0
          | |
          | +--> Sub-Macro set_page_count (include/linux/mm.h): static inline void set_page_count(struct page *page, int count) { atomic_set(&page->_count, count); } // _count = -count for ref (negative used/free)
          | | [Rationale: Init refcount (_count = -1 used kernel, 0 free, -ref refd); e820_pfn_ok(pfn): if (e820 type for pfn PAGE in RAM (1 or 4)) return 1; 2.6.20: Loop 1GB/4KB=262144 iters ~50ms; global no, mem_map[pfn]]
          | | [e.g., for pfn=0x1000 (4MB) to 0x3F000: if e820 RAM page_to_pfn(page)==pfn (identity), atomic_set(&_count, 1) i.e. -1 free? Wait, set_page_count(page, 1) means _count.counter =1, but convention -1 free? Wait, in 2.6.20 set_page_count(page, 1) for free (positive free, negative ref); printk "Freeing %luk lowmem" total]
          | | [Error: Bad loop (max_low_pfn large) -> time out (but early no timer); corrupt mem_map -> page alloc bug later]
          |
          +--> 2.6.20 Specifics: No memblock (added 2.6.25 for resvd); bootmem free_all_bootmem after slab in mm_init; max_pfn = max(max_low_pfn, high_start_pfn); zone_spanned_pages set in free_area_init; printk mem stats "Memory: %luk/%luk OK (%luk highmem)" in mem_init end
          | |
          | +--> [Overall: ~220-250ms (bootmem scan ~20ms, zonelist build ~10ms, alloc ~5ms, loop ~100ms); output: "Memory: 1024000k/1048576k available (2816k kernel code, 12352k reserved, 1024k data, 256k bss)" printk; ERR: Alloc fail -> panic("Out of memory"); Rationale: MM base (pages for slab/alloc, zones for GFP); next per-CPU phase 19]
    |
    v
Per-CPU & SMP Prep (Phase 19: setup.c cont; ~250-270ms; [C]; setup per-cpu areas (pcpu_base_addr); smp_prepare_cpus (APIC probe); 2.6.20: UP default max_cpus=1; no x2apic; basic ACPI MADT parse; globals: __per_cpu_offset[NR_CPUS], pcpu_base_addr)








Parallel: Idle Loop Start (cpu_idle() on CPU0)
[init/main.c rest_init -> schedule(); ~400ms+; [ASM/C]; post-rest_init; starts multitasking on CPU0; enters idle loop (proc0 PID0); enables IRQs/preemption; spawns kthreadd/kernel_init threads async; globals: current (task_struct *), init_task (PID0), need_resched (int), jiffies (unsigned long); 2.6.20: O(1) scheduler prio_array; state: PE=1 PG=1 WP=1 IRQ=0 -> IRQ=1 PREEMPT=1; from rest_init end; parallel with kernel threads; output: None (silent hlt); error: Bad schedule -> stuck idle (no threads, panic later if no init)]
    |
    +--> First Schedule Call (Line ~500 in rest_init): schedule(NULL); // First call to scheduler; NULL = prev task (no context); starts multitasking, CPU0 enters idle
          |
          +--> Sub-Func schedule (kernel/sched.c Line ~1200-1500): if (preempt_count() || irqs_disabled()) { /* Early no */ } else { /* Full */ pick_next_task(); context_switch(prev, next); } // But early: no tasks, next = &idle_task (init_task PID0)
          | |
          | +--> pick_next_task: for (prio=0; prio < MAX_PRIO; prio++) { if (!list_empty(&active_array[prio].queue)) { next = list_entry(active_array[prio].queue.next, task, run_list); break; } } // O(1) scan prio arrays
          | | [Rationale: O(1) scheduler (prio_array[0-139 RT/normal]); 2.6.20: No CFS (added 2.6.23); active_array global struct prio_array[140]; list_empty = (head.next == &head); error: No next (empty queues) -> next = &idle_task]
          | | [Global: struct task_struct *current = &init_task; struct prio_array active_array[140]; struct runqueue *rq = &per_cpu(runqueue, 0);; 2.6.20: Single CPU rq; MAX_PRIO=140 (100 RT +40 normal)]
          | | [e.g., queues empty early (no kthreads yet); next = idle_task (prio MAX_PRIO-20=120 nice0); printk "Switch to idle" if debug]
          |
          +--> Context Switch ASM Stub (Line ~1300): if (prev == next) return; // Same task; else switch_to(prev, next, prev); // ASM: pushl %ebp; movl %esp, TASK_STATE(%ebx); /* Save prev */ movl TASK_STATE(%esi), %esp; popl %ebp; /* Restore next */
          | |
          | +--> switch_to Macro: asm volatile("pushfl\n\t" "cli\n\t" "movl %%esp, %0\n\t" "movl %3, %%esp\n\t" "popl %1\n\t" "popfl\n\t" : "=m" (prev->thread.esp), "=r" (prev->thread.eip) : "c" (next), "d" (next->thread.esp) : "memory"); // Save prev ESP/EIP, load next ESP, return EIP to next
          | | [Rationale: ASM context save/restore (ESP stack, EIP ret); CLI disable IRQ during switch; 2.6.20: Basic (no FPU save TS=0); global struct thread_struct {unsigned long esp0; unsigned long eip; ...}; error: Bad ESP (corrupt stack) -> #SS seg fault]
          | | [e.g., prev=&init_task (but first no prev); next=&idle_task; switch_to loads idle ESP=0xC0303FFC from phase 13; return to idle code]
          |
    |
    v
Idle Loop Entry (cpu_idle; kernel/sched.c Line ~2000; ~400ms+; [C/ASM]; CPU0 enters idle proc loop; hlt for power save; checks need_resched; enables IRQs/preemption; parallel threads schedule)
    |
    +--> cpu_idle Entry: asmlinkage void cpu_idle(void) [Called from schedule when next=idle_task; PID0 init_idle(current, 0); sets policy=SCHED_NORMAL nice=0 prio=120]
          |
          +--> Preemption Enable: preempt_enable_no_resched(); // Decrement preempt_count; if ==0 allow resched but no check yet; [Rationale: Enable preemption post-switch; 2.6.20: Inline current->preempt_count--; no irq save]
          | |
          | +--> Global: int preempt_count in task_struct; [Rationale: Kernel threads have count=1; user=0; 2.6.20: No RT; error: Count underflow -> bad resched (stuck)]
          |
          +--> Local IRQ Enable: local_irq_enable(); // STI asm("sti"); set IF=1 (enable maskable IRQs); [Rationale: Start IRQs post-setup (PIC/IDT ready); timer IRQ0 ticks jiffies]
          | | [Global: unsigned long irq_flags; local_irq_save/restore for atomic; 2.6.20: STI direct; error: STI with bad IDT -> spurious #GP (handlers stub do_IRQ -> do nothing early)]
          | | [e.g., sti; IRQ0 timer=do_timer_interrupt -> do_timer(1) jiffies++; xtime_update; ~4ms ticks HZ=250]
          |
          +--> Idle Loop: while (1) { if (need_resched()) { schedule(); continue; } default_idle(); } // Check resched flag; if no, hlt power save
          | |
          | +--> need_resched Global: int need_resched; [in task_struct; set by timer IRQ if time slice expire; check_preempt_curr(rq, idle, -1); 2.6.20: O(1) check in loop ~1us]
          | | [Rationale: Yield if runnable tasks; idle prio low (120); error: Infinite loop if no resched (threads blocked, init panic later)]
          | |
          +--> default_idle Sub-Func (kernel/sched.c Line ~2100): if (cpu_has_xsave) xsave idle opt; else if (pm_idle) pm_idle(); else { asm("hlt"); } // HLT CPU power save (wait IRQ); sti implicit in loop? No, sti before loop
          | | [Rationale: Power mgmt; 2.6.20: CONFIG_CPU_IDLE=n default hlt; asm("hlt"); wakes on any IRQ (timer first); global int pm_idle_saved_state; error: No hlt (old CPU) -> busy loop waste power]
          | | [e.g., hlt ~1 cycle entry, waits IRQ; first wake timer or kthread; printk "CPU0 idle loop" if debug]
          |
          +--> Parallel Thread Spawn (From rest_init pre-schedule): kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES); // PID2 daemon for modules; kernel_thread(kernel_init, NULL, CLONE_FS | CLONE_SIGHAND); // Kernel init thread
          | |
          | +--> kernel_thread Sub-Func (kernel/fork.c Line ~1000): clone_flags = CLONE_VM | CLONE_UNTRACED; do_fork(clone_flags | SIGCHLD, 0, &regs, 0, NULL, NULL); // Fork new task; regs.esp = fn arg, eip = kernel_thread_helper ASM stub
          | | [Rationale: Spawn threads async (schedule runs them); CLONE_FS share fs, CLONE_FILES share files, CLONE_SIGHAND share sig; 2.6.20: No CLONE_PARENT_SETTID; global struct task_struct *kthreadd_task; error: Fork fail (no mem) -> panic rare early]
          | | [e.g., kthreadd PID=2, fn=kthreadd (loop wait kthread_create); kernel_init PID=1 (do_basic_setup, exec /sbin/init); helper ASM: mov %ebx, %eax; call *%esi; (fn(arg), exit)]
          |
          +--> 2.6.20 Specifics: init_task PID0 idle prio=MAX_PRIO-20=120; rq = &per_cpu(init_rq, 0); no cpu_idle per-cpu (single); hlt no MWAIT (post-Nehalem); threads schedule on first timer IRQ
          | |
          | +--> [Overall: ~400ms+ ongoing; silent; ERR: No threads spawn -> stuck idle (no init exec, system halt); Rationale: Steady state kernel (idle 99%, threads async); parallel kthreadd spawns keventd etc.; next kernel_init thread phase 31]
    |
    v
kthreadd Daemon (Phase 30: kernel/kthread.c; ~410-420ms; [C]; async thread PID2; waits for kthread_create calls; spawns modules/hotplug; 2.6.20: Spawns keventd for /sbin/hotplug; state: PREEMPT=1 IRQ=1)







kernel_init() Thread Start (main.c)
[init/main.c; ~420-430ms; [C]; Line ~550; protected mode C; kernel_init thread entry (PID1, forked from rest_init kernel_thread); runs do_basic_setup (drivers/fs); prepare_namespace (mount root); sets SYSTEM_RUNNING; calls init_post (free initmem, exec /sbin/init); globals: system_state (enum), root_mountflags (unsigned long); 2.6.20: SysV init exec; state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; async from idle loop phase 29; next do_basic_setup phase 21; output: Printk "Kernel command line..." /proc/cmdline; error: No root mount -> panic "VFS: Unable to mount root fs"; thread exit(0) after exec]
    |
    +--> Thread Entry: int kernel_init(void *unused) [Line ~550; kernel_thread(kernel_init, NULL, CLONE_FS | CLONE_SIGHAND); PID1 (init_task clone); unused=NULL; runs in kernel space (ring0)]
          |
          +--> Proc Cmdline Create (Line ~555): create_proc_read_entry("cmdline", S_IRUSR | S_IRGRP | S_IROTH, NULL, &cmdline_proc_ops, NULL); // /proc/cmdline = boot_command_line str
          | |
          | +--> Sub-Func create_proc_read_entry (fs/proc/generic.c Line ~100): struct proc_dir_entry *pde = kmem_cache_alloc(proc_inode_cachep, GFP_KERNEL); pde->name = "cmdline"; pde->mode = 0444; pde->read_proc = cmdline_read_proc; pde->data = NULL; proc_register(&proc_root, pde); // read_proc: seq_printf(m, "%s\n", boot_command_line);
          | | [Rationale: Expose cmdline to user (cat /proc/cmdline); 2.6.20: seq_file? No, old read_proc fops; global char *boot_command_line; error: kmalloc fail -> no /proc/cmdline (minor, dmesg shows)]
          | | [e.g., cat /proc/cmdline: "root=/dev/sda1 ro quiet"; S_IFREG | S_IRUGO = 0444 read-only; parent=NULL = /proc root]
          |
          +--> Basic Setup Call (Line ~560): do_basic_setup(); // Init cpusets, usermodehelper, bdi, driver_init, do_initcalls (subsys/fs/device levels)
          | |
          | +--> Sub-Func do_basic_setup (main.c Line ~600-700): cpuset_init_smp(); usermodehelper_init(); bdi_init(&noop_backing_dev_info); driver_init(); do_initcalls(); // driver_init: bus_register(ide_bus_type); do_initcalls runs __initcalls (postcore to device)
          | | [Rationale: Basic subsys (cgroups prep, modprobe helper, backing dev, buses); 2.6.20: No dm (device-mapper later); global struct bus_type ide_bus_type; error: Initcall fail -> printk "initcall %p returned %d" (continue, no panic)]
          | | [e.g., cpuset_init_smp: cpuset_init() sets root_cpuset (all CPUs); usermodehelper_init: uevent_helper = "/sbin/hotplug"; do_initcalls ~250 calls (net_init, pci_init, ide_init)]
          |
          +--> Namespace Prepare (Line ~565): prepare_namespace(); // Mount rootfs (pivot if initrd); mount /proc /sys /dev
          | |
          | +--> Sub-Func prepare_namespace (init.c Line ~800): if (initrd_start) { root_mountflags |= MS_RDONLY; pivot_root("/initrd", "/initrd/initrd"); } mount_block_root("/dev/root", "rootfstype"); mount("proc", "/proc", "proc", 0, ""); mount("sysfs", "/sys", "sysfs", 0, ""); mount("devpts", "/dev/pts", "devpts", 0, ""); mount("tmpfs", "/dev/shm", "tmpfs", 0, NULL); // devtmpfs later
          | | [Rationale: Mount root (ext3 from root= /dev/sda1); pivot_root swaps initrd to real root if present; 2.6.20: No devtmpfs default (ramfs /dev); global unsigned long root_mountflags; error: Mount fail -> panic "VFS: Unable to mount root fs on %d:%d" (ROOT_DEV)]
          | | [e.g., mount_block_root: get_super_bdev(root_fstype="ext3", root_dev=MKDEV(3,1) from parse, flags=MS_RDONLY); read_super -> ext3_read_super; submit_bh(READ) I/O via ide_end_request; success "VFS: Mounted root (ext3) on device 3:1"]
          |
          +--> System State Set (Line ~570): system_state = SYSTEM_RUNNING; // From BOOTING to RUNNING; wake_up(&system_wq); // Signal threads waiting (e.g., halt paths)
          | |
          | +--> Global: enum system_states system_state; [in kernel.h; RUNNING=1; Rationale: Allow user helpers (usermodehelper), halt/restart; 2.6.20: wake_up wakes processes on wait_queue_head system_wq]
          | | [e.g., system_state=1; printk "Kernel running"; error: Wrong state -> helpers fail (modprobe stuck)]
          |
          +--> Proc Caches Init (Line ~575): proc_caches_init(); // kmem_cache_create("proc_inode_cache", sizeof(struct proc_inode), ...); for /proc inodes
          | |
          | +--> Sub-Func proc_caches_init (fs/proc/proc.c Line ~50): kmem_cache_create("proc_inode_cache", sizeof(struct proc_inode), 0, SLAB_HWCACHE_ALIGN, NULL); kmem_cache_create("proc_dir_entry_cache", sizeof(struct proc_dir_entry), 0, SLAB_PANIC, NULL); // SLAB_PANIC panic on alloc fail
          | | [Rationale: Slab caches for /proc efficiency; 2.6.20: SLAB only (no SLUB); global struct kmem_cache *proc_inode_cachep; error: Slab create fail -> panic("Out of mem for proc cache")]
          | | [e.g., proc_inode_cache ~256B (dentry + inode); used in proc_create; /proc/cmdline read from cache]
          |
          +--> Init Post Call (Line ~580): init_post(); // Free initmem, mark rodata RO, proc_sys_init, run_init_process("/sbin/init")
          | |
          | +--> Sub-Func init_post (main.c Line ~900): free_initmem(); mark_rodata_ro(); proc_sys_init(); run_init_process("/sbin/init"); // run_init_process: do_execve("/sbin/init", {"/sbin/init"}, environ); fallback /etc/init /bin/sh
          | | [Rationale: Cleanup and handover to user; 2.6.20: free_initmem slab_free __init sections (~2-4MB); mark_rodata_ro set_memory_ro(rodata_start, rodata_len); exec PID1 SysV init]
          | | [e.g., do_execve loads ELF /sbin/init as PID1; reparents orphans; error: No init found -> panic "No init found. Try passing init= option"]
          |
          +--> Thread Exit (Line ~585): return 0; // Kernel thread exit; do_exit(0); -> schedule to idle or next
          | |
          | +--> [Rationale: Handover complete; total ~420-430ms (setup ~200ms dominant); output: "Kernel command line: ..." /proc; ERR: Mount fail -> panic; Rationale: Final kernel setup before user; 2.6.20: No cgroup v1 full (basic cpusets); next do_basic_setup phase 21]
    |
    v
do_basic_setup() (Phase 21: main.c in kernel_init; ~430-500ms; [C]; cpuset init, usermodehelper, driver init, do_initcalls; 2.6.20: No dm-crypt early; basic bdi)








do_basic_setup() (main.c)
[init/main.c; ~430-500ms; [C]; Line ~600-700; protected mode C in kernel_init thread; initializes basic subsystems (cgroups prep via cpuset, user helpers for modprobe, backing dev info for writeback, drivers/buses, all initcalls from postcore to device level); globals: struct bus_type *ide_bus_type, struct workqueue_struct *keventd_wq, unsigned long root_mountflags; 2.6.20: No device-mapper crypt early (subsys_initcall dm_crypt_init later); basic noop_bdi for tmpfs; state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; post-phase 20 kernel_init entry; next prepare_namespace phase 36; output: Printk "driver core: built-in: %s" for buses, initcall returns if fail; error: Initcall -ENOMEM -> printk "initcall %p returned -12" continue (no panic unless SLAB_PANIC)]
    |
    +--> CPUs et Init SMP (Line ~600): cpuset_init_smp(); // Early cgroup cpuset prep for SMP (root cpuset all CPUs); if CONFIG_SMP=y sets cpuset_init() with allowed CPUs
          |
          +--> Sub-Func cpuset_init_smp (kernel/cgroup.c Line ~100-150, but 2.6.20 kernel/cpuset.c): struct cpuset *root = &root_cpuset; root->cpus_allowed = ~0UL; root->mems_allowed = ~0UL; root->flags = 1; INIT_LIST_HEAD(&root->css.cgroup->children); // Root cgroup full access
          | |
          | +--> Global: struct cpuset root_cpuset; [in cpuset.c; struct cgroup_subsys cpuset_subsys = { .name = "cpuset", .subsys_id = cpuset_subsys_id, .early_init = 1 }; Rationale: Early for SMP boot APs (smp_prepare_cpus limits cpuset); 2.6.20: Basic cgroups (no v1 full, cpuset only); error: No mem -> skip (single CPU fallback)]
          | | [e.g., cpus_allowed = 0xFFFFFFFF (all 32 CPUs); mems_allowed = 0xFFFFFFFF (all nodes); printk "cpuset: root set all CPUs"; 2.6.20: CONFIG_CPUSETS=y opt]
          |
          +--> Usermode Helper Init (Line ~605): usermodehelper_init(); // Setup /proc/sys/kernel/{uevent_helper,modprobe} for user-space helpers (modprobe, hotplug)
          | |
          | +--> Sub-Func usermodehelper_init (kernel/kmod.c Line ~100-150): uevent_helper = "/sbin/hotplug"; modprobe_path = "/sbin/modprobe"; // Defaults; register /proc/sys/kernel/uevent_helper write to set custom
          | | [Rationale: Enable exec user cmds from kernel (request_module for .ko, uevent for udev); 2.6.20: /proc write uevent_helper = call_usermodehelper_setup (set path, exec /sbin/hotplug); global char *uevent_helper, *modprobe_path; error: Bad path -> default /sbin/modprobe fail (module load stuck)]
          | | [e.g., call_usermodehelper("/sbin/modprobe", argv={"modprobe", "-s", "-q", "--", "ext3"}, envp, UMH_WAIT_EXEC); execve via do_execve; 2.6.20: No systemd, SysV hotplug]
          |
          +--> Backing Dev Info Init (Line ~610): bdi_init(&noop_backing_dev_info); // Init noop BDI for tmpfs/ramfs writeback (no cache flush)
          | |
          | +--> Sub-Func bdi_init (mm/backing-dev.c Line ~50-100): struct backing_dev_info *bdi = &noop_backing_dev_info; bdi->ra_pages = 0; bdi->capabilities = BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK; init_timer(&bdi->wb.wb_wait); bdi->wb.dirty_exceeded = 0; // No dirty tracking
          | | [Rationale: BDI for filesys writeback (balance_dirty_pages); noop for non-block (proc/sysfs); 2.6.20: Basic wb (writeback) struct; global struct backing_dev_info noop_backing_dev_info; error: Timer init fail -> no wb wait (dirty stall rare)]
          | | [e.g., bdi_register(&noop_bdi, "noop"); used in tmpfs_writepage; printk "bdi: noop registered"; 2.6.20: No cfq elevator bdi yet]
          |
          +--> Driver Init (Line ~615): driver_init(); // Register all buses (PCI, IDE, SCSI); calls bus_register for each
          | |
          | +--> Sub-Func driver_init (drivers/base/init.c Line ~50): extern struct bus_type pci_bus_type; bus_register(&pci_bus_type); bus_register(&ide_bus_type); bus_register(&scsi_bus_type); // Etc. for all built-in buses
          | | [Rationale: Setup /sys/bus (sysfs); bus_register: kobject_init(&bus->kobj, &bus_ktype); sysfs_create_dir; 2.6.20: ide_bus_type {name="ide", match=ide_bus_match, probe=ide_bus_probe}; global struct bus_type ide_bus_type; error: Kobj fail -> no /sys/bus/ide (udev fail)]
          | | [e.g., pci_init calls bus_register(&pci_bus_type); /sys/bus/pci/devices/00:1f.1 (IDE controller); 2.6.20: No platform bus early]
          |
          +--> Initcalls Execution (Line ~620): do_initcalls(); // Run all registered init functions by level (pure/core/postcore/arch/subsys/fs/device/late); ~250 calls
          | |
          | +--> Sub-Func do_initcalls (main.c Line ~850): static initcall_t *initcall_levels[] = {&__initcall_start[0], &__initcall_start[1], ... &__initcall_end[6]}; for (int level=0; level<7; level++) { for (initcall_t *call = initcall_levels[level]; call < initcall_levels[level+1]; call++) { int err = do_one_initcall(*call); if (err <0) printk(KERN_CRIT "initcall %p returned %d\n", *call, err); } }
          | | [Rationale: Modular init by dep order (pure early mem, core slab, postcore rcu, arch smp, subsys pci/ide, fs ext3, device ide_disk, late calibrate); 2.6.20: ~250 calls (e.g., postcore_initcall(rcu_init_node), subsys_initcall(pci_init), fs_initcall(ext3_init), device_initcall(ide_disk_init)); global extern initcall_t __initcall_start[], __initcall_end[]; linker sections]
          | | [e.g., level0 pure: mem_init(); level1 core: kmem_cache_init(); level4 subsys: pci_init (pci_scan_bus(0)); level5 fs: register_filesystem(&ext3_fs_type); level6 device: ide_probe_module (probe hwifs/drives); err=-ENOMEM printk but continue; error: Critical fail (e.g., slab panic) -> kernel halt]
          | | [do_one_initcall: if (!fn) printk "No fn"; else { struct trace_initcall trace = {fn}; trace_event_initcall(&trace); err = fn(); trace_event_finishcall(&trace); } no trace in 2.6.20 (added later)]
          |
          +--> 2.6.20 Specifics: cpuset_subsys.early_init=1; uevent_helper="/sbin/hotplug" for udev; noop_bdi.capabilities = BDI_CAP_NO_ACCT_DIRTY (no dirty bytes track); initcalls levels strict (no deferred, fail continue); no blk-mq (legacy ll_rw_blk)
          | |
          | +--> [Overall: ~430-500ms (initcalls ~300ms dominant, ~250 calls ~1ms each); output: "driver core: built-in: ide-pci..." /sys/bus; ERR: Alloc fail in initcall -> -ENOMEM log continue (system partial, e.g., no PCI probe); Rationale: Subsys ready for hardware (buses for probe, helpers for modules); next prepare_namespace mount]
    |
    v
prepare_namespace() (Phase 36: init.c in kernel_init; ~750-850ms; [C]; mounts rootfs (pivot initrd if present); mounts /proc /sys devpts tmpfs; 2.6.20: Ramfs for /dev early, no devtmpfs default)





Softirq Init (softirq.c)
[include/linux/interrupt.h & kernel/softirq.c; ~300-310ms; [C]; Line ~50-100; protected mode C in start_kernel after trap_init; initializes softirq vectors (bottom halves for deferred IRQ work); registers 6 softirq types (NR_SOFTIRQS=6); sets tasklet_action for bottom halves; globals: struct softirq_action softirq_vec[NR_SOFTIRQS], open_softirq_count; 2.6.20: Classic softirq (no RCU boost, tree_rcu later); no softirqd ksoftirqd per-CPU (added 2.6.25); state: PE=1 PG=1 WP=1 IRQ=1 masked (unmask in rest_init); post-phase 21 trap_init IDT; next time_init phase 24; output: None (silent); error: No mem for tasklet -> printk "tasklet_init failed" (continue, no panic)]
    |
    +--> Softirq Vector Clear (Line ~50): for (int i=0; i<NR_SOFTIRQS; i++) { softirq_vec[i].action = NULL; softirq_vec[i].flags = 0; } // Reset all 6 softirq slots to unused
          |
          +--> Global: struct softirq_action softirq_vec[NR_SOFTIRQS]; [NR_SOFTIRQS=6 in interrupt.h; struct { void (*action)(struct softirq_action *); unsigned long flags; }; Rationale: Array for vec lookup in do_softirq; 2.6.20: Flags for pending (bit0 set in __raise_softirq_irqoff); error: Array overflow (i>5) -> corrupt next mem (rare, hardcoded loop)]
          | |
          | +--> [e.g., softirq_vec[0].action = NULL; pending mask 0; 2.6.20: No per-CPU pending (global early); used in open_softirq to assign action]
          |
          +--> Register Softirq Handlers (Line ~55-70): open_softirq(TASKLET_SOFTIRQ, tasklet_action); open_softirq(HI_SOFTIRQ, raise_softirq); open_softirq(NET_TX_SOFTIRQ, net_tx_action); open_softirq(NET_RX_SOFTIRQ, net_rx_action); open_softirq(BLOCK_SOFTIRQ, blk_done); open_softirq(TASKLET_SOFTIRQ, tasklet_hi_action); // 6 total: tasklet (0), hi-tasklet (5), net_tx(1), net_rx(2), block(3), timer? Wait, timer BH in bh.c not softirq
          | |
          | +--> Sub-Func open_softirq (unsigned int nr, void (*action)(struct softirq_action *)): if (nr >= NR_SOFTIRQS) { printk(KERN_ERR "open_softirq: vec %d bad\n", nr); return; } softirq_vec[nr].action = action; // Assign handler; open_softirq_count++; [Rationale: Register deferred handler for vec; called once per type; 2.6.20: TASKLET_SOFTIRQ=0 (bottom halves), HI_SOFTIRQ=5 (high prio tasklets), NET_TX=1 (netif_tx), NET_RX=2 (netif_rx), BLOCK=3 (blk_done for I/O complete)]
          | | [Global: extern int open_softirq_count; [in softirq.c; count=6 post-init; used in show_softirqs /proc for stats; error: Duplicate open (action overwrite) -> wrong handler call in do_softirq)]
          | | [e.g., open_softirq(0, tasklet_action); tasklet_action(struct softirq_action *h): do_softirq(&tasklet_vec); (runs queued tasklets); 2.6.20: No RCU_SOFTIRQ (added 2.6.32); block_softirq = blk_done (end_request for I/O)]
          |
          +--> Tasklet Init (Line ~75): tasklet_init(&tasklet_vec, do_softirq, NULL); // Init tasklet for softirq bottom halves (run queued tasks)
          | |
          | +--> Sub-Func tasklet_init (kernel/softirq.c Line ~200-250): struct tasklet_struct *t = &tasklet_vec; t->next = NULL; t->state = 0; t->func = do_softirq; t->data = 0; atomic_set(&t->count, 0); // Add to tasklet_list? No, global tasklet_vec for softirq
          | | [Rationale: Tasklet for deferred work (irq-safe); do_softirq runs all pending softirqs; 2.6.20: Global struct tasklet_struct tasklet_vec, tasklet_hi_vec; state=0 (STATE_SCHED=1 pending, STATE_RUN=2 running); error: Atomic set fail (count overflow) -> stuck tasklet]
          | | [e.g., tasklet_vec.func = do_softirq (loop vec if pending run action); __raise_softirq_irqoff(vec, flags); sets pending; 2.6.20: No hi-tasklet separate init (open_softirq(5, tasklet_hi_action))]
          |
          +--> Softirq Pending Mask Init (Line ~80): for (int i=0; i<NR_SOFTIRQS; i++) local_softirq_pending() = 0; // Per-CPU? Early global; [Rationale: Clear pending for new boot; do_softirq checks local_bh_disable() or pending]
          | |
          | +--> Global: unsigned long local_softirq_pending; [per_cpu in 2.6.20? No, early global; set_bit(vec, &local_softirq_pending); test_bit(vec, &local_softirq_pending); Rationale: Track pending softirqs per-CPU (but single CPU early)]
          | | [e.g., pending=0; __do_softirq: while (pending) { vec = find_first_bit(pending, NR_SOFTIRQS); if (vec < NR_SOFTIRQS) { clear_bit(vec, &pending); h = softirq_vec[vec]; if (h->action) h->action(h); } } ; 2.6.20: No ksoftirqd (poll in do_softirq)]
          |
          +--> 2.6.20 Specifics: NR_SOFTIRQS=6 (tasklet0, net_tx1, net_rx2, block3, tasklet_hi5, irq_poll4? No, irq_poll later); open_softirq_count=6; no RCU_SOFTIRQ (classic RCU callback in timer BH); tasklet_action calls __do_softirq for bottom halves
          | |
          | +--> [Overall: ~300-310ms (loops ~1ms); silent; ERR: Bad nr in open_softirq -> printk ERR continue; Rationale: Deferred IRQ work (net/block/tasklet); next time_init calibrate jiffies; post-trap_init (IDT vecs ready for softirq)]
    |
    v
Time Init (Phase 24: time.c; ~320-340ms; [C]; calibrate TSC/PIT for loops_per_jiffy; sets xtime.tv_sec/nsec=0; jiffies=INITIAL_JIFFIES (~ -300s); 2.6.20: HZ=250, no hrtimers)






RCU Init (rcu.c)
[kernel/rcu.c; ~310-320ms; [C]; Line ~100-150; protected mode C in start_kernel after sched_init; initializes Read-Copy-Update (classic RCU) for lockless read-mostly data structures (e.g., routing tables, dentries); sets up per-CPU data for grace periods; registers notifier for CPU hotplug (early no hotplug); globals: struct rcu_ctrlblk rcu_ctrlblk (classic RCU), struct rcu_bh_ctrlblk rcu_bh_ctrlblk (BH RCU), rcu_preempt_notifier (preempt RCU no in 2.6.20); 2.6.20: Basic classic RCU (no tree-RCU, preempt-RCU added 2.6.24); no RCU boost/ksoftirqd (poll in softirq); state: PE=1 PG=1 WP=1 IRQ=1 masked; post-phase 22 softirq_init; next time_init phase 24; output: None (silent); error: No mem for per-CPU -> printk "rcu_init_percpu_data failed" (continue, RCU disabled, lockless ops fall to locks)]
    |
    +--> Per-CPU Data Init (Line ~100): rcu_init_percpu_data(0, nr_cpu_ids); // Init per-CPU rcu_data for classic RCU; nr_cpu_ids=1 early (SMP full later)
          |
          +--> Sub-Func rcu_init_percpu_data (internal Line ~200-250): for (int cpu = 0; cpu < nr_cpu_ids; cpu++) { struct rcu_data *rdp = &per_cpu(rcu_data, cpu); rdp->qs_pending = 0; rdp->passed_quiescent_check = 0; rdp->qs_pending_mask = 0; rdp->nxtlist = NULL; rdp->nxttail[RCU_NEXT_SIZE - 1] = &rdp->nxtlist; rdp->qlen = 0; rdp->cpu = cpu; INIT_LIST_HEAD(&rdp->donelist); rdp->cpubase = &per_cpu(rcu_ctrlblk, cpu); } // Quiescent state (qs) for grace period end
          | |
          | +--> Globals: struct rcu_data per_cpu(rcu_data, NR_CPUS); [per_cpu var in rcu.h; struct { struct rcu_head *nxtlist; struct rcu_head **nxttail[RCU_NEXT_SIZE=3]; int qlen; int cpu; struct list_head donelist; int qs_pending; int passed_quiescent_check; unsigned long qs_pending_mask; struct rcu_ctrlblk *cpubase; }; Rationale: Per-CPU RCU callbacks (lockless read, batch update); 2.6.20: Classic RCU (single global rcu_ctrlblk, per-CPU rdp for batching); NR_CPUS=1 UP or 32 SMP]
          | | [Rationale: rdp->nxtlist = queued rcu_head for call_rcu (free after grace); qs_pending = need quiescent (context switch/timer); error: Alloc per-CPU fail (kmem_cache_alloc rcu_data_cache) -> printk "rcu_init_percpu_data: kmem_cache_alloc failed" (RCU ops revert to synchronize_kernel lock)]
          | | [e.g., rdp->cpubase = &rcu_ctrlblk; INIT_LIST_HEAD(&donelist); qlen=0; 2.6.20: No preempt-RCU (rdp->preemptible_qs); printk none, debug only]
          |
          +--> RCU Control Block Init (Line ~105): rcu_init_ctrlblk(&rcu_ctrlblk); rcu_init_bh_ctrlblk(&rcu_bh_ctrlblk); // Init global ctrlblks for classic and BH RCU
          | |
          | +--> Sub-Func rcu_init_ctrlblk (internal Line ~300): struct rcu_ctrlblk *rcp = &rcu_ctrlblk; rcp->completed = 0; rcp->rcuctrl = RCU_CTL_INIT; rcp->nxtcompleted = 0; INIT_LIST_HEAD(&rcp->rcucblist); for (int i=0; i<RCU_NUM_LVLS; i++) rcp->nxtlvl[i] = NULL; rcp->qlen = 0; rcp->n_rcu_barrier = 0; // Grace period state
          | | [Rationale: Global ctrlblk for grace periods (readers quiescent -> update safe); 2.6.20: Classic RCU (single gp, no tree); struct rcu_ctrlblk { long completed; struct rcu_head *nxtlist; struct rcu_head **nxttail[RCU_NUM_LVLS=3]; int qlen; int n_rcu_barrier; long nxtcompleted; struct list_head rcucblist; int cpu_kthreads; int cpumask; }; error: List head corrupt -> call_rcu list_add fail (leak callbacks)]
          | | [Global: extern struct rcu_ctrlblk rcu_ctrlblk; extern struct rcu_bh_ctrlblk rcu_bh_ctrlblk; (BH variant for softirq); Rationale: rcu_ctrlblk for task context, bh for BH; 2.6.20: No preempt gp (preempt_rcu later); INIT_LIST_HEAD(&rcucblist) for cpu kthreads list]
          | | [e.g., completed=0 (current gp); rcuctrl = RCU_CTL_INIT (state idle); qlen=0 queued; 2.6.20: No rcu_barrier kthread early]
          |
          +--> CPU Notifier Register (Line ~110): register_cpu_notifier(&rcu_preempt_notifier); // For hotplug (early no hotplug); but 2.6.20 classic no preempt notifier
          | |
          | +--> Sub-Func register_cpu_notifier (kernel/notifier.c Line ~50): list_add_tail(&notifier->list, &cpu_chain_head); // Add to notifier list; [Rationale: Callback on CPU online/offline (rcu_cpu_notify); 2.6.20: Basic notifier_call_chain (loop list call ->notifier_call); global ATOMIC_NOTIFIER_HEAD(cpu_chain); error: List add fail (mem) -> no hotplug RCU adjust (SMP safe early)]
          | | [e.g., rcu_preempt_notifier.notifier_call = rcu_preempt_notify; (but 2.6.20 classic no preempt, stub fn=rcu_notify); notifier_block { notifier_call, priority=0, next=NULL }; 2.6.20: No hotplug early (smp_init full)]
          |
          +--> Grace Period Init (Line ~120): rcu_gp_init(); // Set initial gp state; but 2.6.20 basic set completed=0 in ctrlblk
          | |
          | +--> [Rationale: Prep for call_rcu (queue callback, wait gp); 2.6.20: Classic single gp (force_quiescent_state scans CPUs for qs); no tree (multi-gp later); error: No init -> first call_rcu leak (deref after free)]
          |
          +--> 2.6.20 Specifics: rcu_data_cache = kmem_cache_create("rcu_data", sizeof(struct rcu_data), 0, SLAB_NO_DEBUG, NULL); (early alloc rdp); no rcu_bh_qs (BH RCU separate); no rcu_boost kthread (poll); globals rcu_pending (per-CPU pending gp), rcu_qs_ctr (quiescent counter)
          | |
          | +--> [Overall: ~310-320ms (loops ~5ms, kmem ~10ms); silent; ERR: Cache alloc fail -> printk "rcu: kmem_cache_create failed"; RCU off (synchronize_rcu = synchronize_kernel spinlock); Rationale: Lockless read (rcu_read_lock/unlock, call_rcu for update); used in net/routing, VFS dentries; next time_init calibrate jiffies for RCU timer]
    |
    v
Scheduler Init (Phase 25: sched.c; ~340-380ms; [C]; O(1) prio_array init; idle task setup PID0; BH init; 2.6.20: 140 prios, no CFS)






Time Init (time.c)
[kernel/time/time.c; ~320-340ms; [C]; Line ~200-300; protected mode C in start_kernel after RCU; calibrates TSC (RDTSC) and PIT (8253 timer) for loops_per_jiffy (lpj) used in delay loops and jiffies; sets xtime.tv_sec/nsec=0 (wall time start); jiffies = INITIAL_JIFFIES (~ -300s offset for uptime); initializes timekeeper struct; globals: unsigned long loops_per_jiffy (lpj), unsigned long jiffies, struct timespec xtime, struct timekeeper timekeeper; 2.6.20: HZ=250 (CONFIG_HZ=250), no highres timers (ntimers added 2.6.21); PIT 1193180 Hz /18 =55ms tick; state: PE=1 PG=1 WP=1 IRQ=1 masked (unmask in rest_init); post-phase 23 RCU; next VFS caches phase 26; output: Printk "Calibrating delay loop... %ld.%02ld BogoMIPS (lpj=%lu)" if verbose; error: Bad calibrate (lpj=0) -> fallback PIT only, delays wrong (system time drift)]
    |
    +--> TSC Calibration (Line ~200): unsigned long tsc_khz = calibrate_tsc(); // RDTSC loop for TSC freq in kHz; if (tsc_khz) { /* Use TSC */ lpj = tsc_khz * 1000 / HZ; } else calibrate_pit(); // Fallback PIT
          |
          +--> Sub-Func calibrate_tsc (internal Line ~500-550): unsigned long start = rdtsc(); calibrate_delay_loop(1000000, 1); unsigned long end = rdtsc(); unsigned long tsc_loops = end - start; lpj = (tsc_loops * HZ) / 1000000; return (tsc_loops * 1000) / 1000000; // kHz = loops /1ms *1000
          | |
          | +--> rdtsc ASM: asm volatile("rdtsc" : "=A" (tsc) : : "memory"); // %eax=low, %edx=high, tsc = (u64)low | ((u64)high <<32); [Rationale: TSC (Time Stamp Counter) increments at CPU freq; calibrate_delay_loop(1M loops ~1s PIT); 2.6.20: asm in include/asm-i386/timex.h; global no, local tsc]
          | | [Global: unsigned long loops_per_jiffy; [in include/asm-i386/timex.h; used in delay (loops * HZ /1000 us); Rationale: CPU speed for udelay/mdelay; error: TSC non-monotonic (bad CPU) -> lpj=0, fallback pit_calibrate_tsc(lpj, tsc_khz=0, 50ms)]
          | | [e.g., start=0x0000000012345678, end=0x0000000023456789, tsc_loops ~0x11111111 (2GHz *1s =2e9); lpj = 2e9 /4 =5e8 (HZ=250, 2e9 loops/jiffy); tsc_khz = 2e9 /1e6 *1e3 =2000 MHz; printk "Detected %ld.%ld MHz processor" in calibrate_delay]
          |
          +--> PIT Fallback Calibrate (Line ~210): if (!tsc_khz) { pit_calibrate_tsc(lpj, 0, 50); } // PIT 8253 timer 1.193180 MHz /12 =99.5kHz, div 18 for 55ms tick; loop 50ms for lpj
          | |
          | +--> Sub-Func pit_calibrate_tsc (internal Line ~550-600): unsigned long d1 = loops_per_jiffy, d2, d3; outb(0x34, 0x43); outb(0x00, 0x40); outb(0x00, 0x40); // PIT ch0 mode2 rate gen low byte; unsigned long start = rdtsc(); delay_1ms(); unsigned long end = rdtsc(); d2 = end - start; // 1ms TSC; lpj = d2 * 1000 / HZ; // Adjust for 1ms
          | | [Rationale: PIT fallback if no TSC or bad; outb PIT cmd 0x43 mode2 (rate gen), data 0x40 low/high for div 65536 (1.19MHz /65536 ~18Hz); delay_1ms = calibrate_delay_loop(1000, 0); 2.6.20: asm outb in i8253.c; global no, local d1/d2]
          | | [e.g., d2 ~2000 (2GHz *1ms); lpj = 2000 *1000 /250 =8e6; printk "Calibrating delay via PIT loop" if fallback; error: PIT locked (CMOS) -> lpj=0, delays broken (udelay busy loop infinite)]
          |
          +--> xtime Wall Time Zero (Line ~220): do { xtime.tv_nsec = 0; xtime.tv_sec = 0; } while (xtime.tv_nsec); // Initial wall time 0 (uptime from boot); [Rationale: Monotonic time start; used in do_gettimeofday]
          | |
          | +--> Global: struct timespec xtime = {0, 0}; [in timekeeping.h; tv_sec seconds since epoch (0 boot), tv_nsec nanos; Rationale: Kernel wall time; 2.6.20: No NTP adjtime early; updated in do_timer]
          | | [e.g., xtime.tv_sec=0; tv_nsec=0; error: Loop (nsec!=0) infinite rare (atomic set); printk none]
          |
          +--> Jiffies Offset Init (Line ~225): jiffies = INITIAL_JIFFIES; // ~ -300s offset (INITIAL_JIFFIES = -300*HZ); [Rationale: Uptime from 1 Jan 1970 (avoid Y2K38? No, 32b overflow 2038); jiffies since boot + offset = wall]
          | |
          | +--> Global: unsigned long volatile jiffies = 0; [in jiffies.h; tick HZ=250 times/s; Rationale: Monotonic ticks for sched/timer; 2.6.20: No jiffies_64 (32b); INITIAL_JIFFIES = -300*HZ ~ -75000 (300s back); error: Bad offset -> time wrong (dmesg uptime off)]
          | | [e.g., jiffies = -75000; do_timer(j) +=1 each tick; get_jiffies_64 = jiffies + INITIAL_JIFFIES; uptime = (get_jiffies_64() / HZ) s]
          |
          +--> Timekeeper Init (Line ~230): timekeeper.tk_time.tv_sec = 0; timekeeper.tk_time.tv_nsec = 0; timekeeper.xtime_nsec = 0; timekeeper.shift = 22; timekeeper.xtime_interval = HZ; // NTP frac adjust 0; shift for nsec calc
          | |
          | +--> Global: struct timekeeper timekeeper = {0}; [in timekeeping.h; struct { struct timespec tk_time; s64 tk_tai_offset; unsigned long xtime_nsec; unsigned long xtime_interval; u32 shift; ... }; Rationale: Master clock (wall + monotonic); 2.6.20: Basic (no NTP frac early); shift=22 (2^-22 frac for nsec); error: Bad interval -> tick drift]
          | | [e.g., xtime_interval = 250 (HZ); next tick add 1 to tk_time; do_timer updates timekeeper]
          |
          +--> 2.6.20 Specifics: calibrate_delay asm in lib/delay.c (loops * HZ /1000 us); no hpet/tsc deadline timer (added 2.6.21); PIT calibrate outb 0x34/0x43 mode2; no clocksource_register (tsc_clocksource later); globals exported lpj (module param)
          | |
          | +--> [Overall: ~320-340ms (calibrate loops ~200ms dominant, 1e6 loops ~100ms); output: "Calibrating delay loop... 1999.80 BogoMIPS (lpj=998902400)" printk; ERR: lpj=0 -> "Time: tsc clocksource not available" fallback PIT; Rationale: Accurate delays/scheduling (udelay uses lpj); next VFS caches phase 26]
    |
    v
VFS Caches Init (Phase 26: init.c; ~380-390ms; [C]; kmem_cache_create dentry/inode caches; 2.6.20: SLAB only, no percpu)





Scheduler Init (sched.c)
[kernel/sched.c; ~340-380ms; [C]; Line ~1000-1200; protected mode C in start_kernel after VFS caches; initializes O(1) scheduler (prio_array[140] for RT/normal tasks); sets up idle task (PID0 init_task); initializes bottom halves (BH) for deferred work (TIMER_BH etc.); globals: struct prio_array init_task_prio (idle prio), struct runqueue *runqueues[NR_CPUS] (early single CPU), struct task_struct init_task (PID0), unsigned long jiffies (for expiry); 2.6.20: O(1) scheduler (prio_array_t[140], active_array[140]); no CFS (added 2.6.23); MAX_PRIO=140 (0-99 RT, 100-139 normal); state: PE=1 PG=1 WP=1 IRQ=1 masked; post-phase 26 VFS caches; next proc_root_init phase 27; output: None (silent); error: Bad prio_array alloc (kmalloc fail) -> panic "sched_init: out of mem" (rare early)]
    |
    +--> O(1) Prio Tree Init (Line ~1000): prio_tree_init(); // Init global prio trees for O(1) sched (bitmap for ready queues)
          |
          +--> Sub-Func prio_tree_init (internal Line ~1500-1550): for (int prio=0; prio < MAX_PRIO; prio++) { struct prio_tree_root *root = &per_cpu(prio_tree_root, 0)[prio]; root->left = NULL; root->right = NULL; root->parent = NULL; } // Early single CPU; full per-CPU in smp_init
          | |
          | +--> Global: struct prio_tree_root per_cpu(prio_tree_root, NR_CPUS)[MAX_PRIO]; [in sched.h; struct { struct prio_tree_node *left, *right; struct prio_tree_node *parent; int index; }; Rationale: RB-tree for prio bitmaps (fast find highest prio ready); 2.6.20: O(1) uses bitmap for active prios; error: No alloc (static per-CPU) -> corrupt tree (wrong sched order)]
          | | [Rationale: O(1) sched (scan bitmaps for highest prio); 2.6.20: No deadline/EEVDF; MAX_PRIO=140; printk none]
          | | [e.g., root->index = prio; used in enqueue_task (rb_insert); 2.6.20: Per-CPU for SMP (NR_CPUS=1 early)]
          |
          +--> Prio Array Init (Line ~1010): for (int prio=0; prio < MAX_PRIO; prio++) { init_prio_array(&active_array[prio], DEQUEUE_SLEEP); } // Init active queues for each prio (RT/normal)
          | |
          | +--> Sub-Func init_prio_array (internal Line ~1600): struct prio_array *array = pa; array->bitmap[0] = 0; array->bitmap[1] = ~0UL; array->prio_tree_root.left = NULL; ... for (int i=0; i<2; i++) INIT_LIST_HEAD(&array->queue[i]); array->nr_running = 0; array->dequeue_flags = flags; // DEQUEUE_SLEEP=1 (sleep tasks)
          | | [Rationale: Per-prio queue (list_head queue[2] for current/sleep); bitmap for fast highest bit find (ffz ~); 2.6.20: active_array global struct prio_array active_array[MAX_PRIO]; DEQUEUE_SAVE=0 (save state), DEQUEUE_SLEEP=1]
          | | [Global: struct prio_array active_array[MAX_PRIO]; [in sched.h; struct { unsigned long bitmap[2]; struct list_head queue[2]; struct prio_tree_root prio_tree_root; int nr_running; int dequeue_flags; }; Rationale: O(1) enqueue/dequeue (list_add_tail to queue[prio %2], set bit bitmap[prio/32]); error: List head corrupt -> enqueue crash list_add]
          | | [e.g., for prio=120 (idle): active_array[120].bitmap[3] = bit120 set on enqueue; nr_running=0 early; 2.6.20: No deadline_array (CFS later)]
          |
          +--> Idle Task Init (Line ~1020): init_idle(current, smp_processor_id()); // Setup PID0 init_task as idle (prio MAX_PRIO-20=120 nice0)
          | |
          | +--> Sub-Func init_idle (internal Line ~1700): struct task_struct *idle = current; idle->pid = 0; idle->state = TASK_RUNNING; init_task_prio = idle->prio_array; set_task_rq(idle, cpu); idle->prio = MAX_PRIO - 20; idle->static_prio = NICE_TO_PRIO(0); // nice=0 prio=120
          | | [Rationale: PID0 idle for CPU0; 2.6.20: smp_processor_id=0 early; global struct task_struct init_task = {pid=0, prio=120, state=0, run_list={next/prev=&run_list}, prio_array=&active_array[120], ...}; error: Bad prio (out 0-139) -> sched crash]
          | | [Global: struct prio_array *init_task_prio; [points to active_array[120]; Rationale: Idle queue for cpu_idle; set_task_rq: idle->rq = &runqueues[cpu]; 2.6.20: No rt_mutex (simple prio)]
          | | [e.g., init_task.state = TASK_RUNNING (0); prio=120 (normal min); printk "init_idle called for CPU 0" if debug]
          |
          +--> Bottom Halves Init (Line ~1030): init_bh(TIMER_BH, timer_bh); init_bh(TIMER_SOFTIRQ, NULL); // Init BH for timer (do_timer), serial etc.; BH as softirq precursor
          | |
          | +--> Sub-Func init_bh (internal Line ~1800): struct tasklet_struct *bh = &bh_base[type]; bh->next = NULL; bh->state = 0; bh->func = func; bh->data = data; // Add to bh_task_vec? No, global array bh_base[NR_BH]
          | | [Rationale: Bottom halves for deferred IRQ (softirq pre); TIMER_BH func=timer_bh (do_timer jiffies++); 2.6.20: 32 BH types (NR_BH=32); global DECLARE_TASKLET(bh_task_vec, do_softirq, 0); error: Duplicate init -> overwrite func (wrong defer)]
          | | [Global: typedef void (*bh_handler_t)(void); extern bh_handler_t do_bh[NR_BH]; extern struct tasklet_struct bh_task_vec; [in bh.h; do_bh[TIMER_BH] = timer_bh; 2.6.20: BH called in do_IRQ bottom half; no softirq migration]
          | | [e.g., init_bh(TIMER_BH=0x0C, timer_bh); timer_bh: do_timer(1); update xtime; 2.6.20: Other BH serial8250, ide, net etc.; printk none]
          |
          +--> SMP Scheduler Prep (Line ~1040): if (smp_num_cpus > 1) sched_init_smp(); // Early SMP stub (full smp_init later); init per-CPU runqueues
          | |
          | +--> Sub-Func sched_init_smp (internal Line ~1900): for (int cpu=1; cpu<smp_num_cpus; cpu++) { runqueues[cpu] = alloc_percpu(struct runqueue); init_runqueue_percpu(cpu); } // Alloc rq per CPU
          | | [Rationale: Per-CPU runqueue for O(1) (active_array per rq); 2.6.20: smp_num_cpus from smp_prepare_cpus; global struct runqueue runqueues[NR_CPUS]; struct runqueue { struct prio_array *active; unsigned long nr_running; spinlock_t lock; ... }; error: Alloc fail -> single CPU fallback]
          | | [e.g., runqueues[0].active = &active_rq[0]; init_runqueue_percpu: rq->active = &active_rq[cpu*MAX_PRIO]; rq->prio_tree_root = &prio_tree_rq[cpu]; 2.6.20: No rt_rq (CFS later)]
          |
          +--> 2.6.20 Specifics: MAX_PRIO=140 (RT 0-99, normal 100-139); DEQUEUE_SAVE=0 (save prio state), DEQUEUE_SLEEP=1 (sleep tasks); no deadline_bandwidth (CFS); init_bh for 32 BH (TIMER_BH=0x0C, IDE_BH=0x0A); globals exported nr_running (per rq)
          | |
          | +--> [Overall: ~340-380ms (prio loops ~20ms, idle init ~1ms, BH ~5ms); silent; ERR: Mem alloc fail in smp -> printk "sched_init_smp: alloc failed" single CPU; Rationale: O(1) ready for multitasking (enqueue/dequeue O(1)); next proc_root_init mount /proc]
    |
    v
VFS Caches Init (Phase 26: vfs_caches_init; ~380-390ms; [C]; kmem_cache_create dentry/inode; 2.6.20: SLAB caches ~512 dentries, 256 inodes)




VFS Caches Init (init.c)
[init/main.c vfs_caches_init(); ~380-390ms; [C]; Line ~400; protected mode C in start_kernel after sched_init; initializes SLAB caches for VFS core data structures (dentries for dir entries, inodes for file metadata); sets initial sizes (~512 dentries, ~256 inodes); globals: struct kmem_cache *dentry_cachep, *inode_cachep; 2.6.20: SLAB allocator only (no SLUB/SLQB added later); initial sizes hardcoded (dentry 512, inode 256); state: PE=1 PG=1 WP=1 IRQ=1 masked; post-phase 25 sched_init; next proc_root_init phase 27; output: None (silent); error: kmem_cache_create fail -> panic("SLAB: Unable to create cache") via SLAB_PANIC flag]
    |
    +--> Dentry Cache Create (Line ~400): kmem_cache_create("dentry_cache", sizeof(struct dentry), 0, SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, NULL); // Cache for dentry structs (~192B each); initial ~512 entries
          |
          +--> Sub-Func kmem_cache_create (mm/slab.c Line ~2000-2100): struct kmem_cache *cachep = kmem_cache_alloc(kmem_cache_cache, GFP_KERNEL); if (!cachep) return NULL; cachep->name = "dentry_cache"; cachep->object_size = sizeof(struct dentry); cachep->align = ARCH_SLAB_MINALIGN (8B); cachep->size = cachep->object_size; cachep->ctor = NULL; cachep->dtor = NULL; cachep->gfpflags = GFP_KERNEL | GFP_DMA; cachep->flags = SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD; slab_init(cachep); // Init slab list, partial slabs
          | |
          | +--> Global: extern struct kmem_cache *dentry_cachep; [in fs/dcache.c; struct dentry { atomic_t d_count; unsigned int d_flags; spinlock_t d_lock; struct inode *d_inode; struct hlist_node d_hash; struct dentry *d_parent; struct qstr d_name; struct dentry_operations *d_op; ... }; Rationale: Cache for dir entries (path walk); SLAB_RECLAIM_ACCOUNT (charge to kmem -1 for reclaim); SLAB_MEM_SPREAD (distribute slabs across nodes)]
          | | [Rationale: Fast alloc/free for dentries (vfs lookup); 2.6.20: object_size=192B (d_name 32B inline); initial_objs = 512 (hardcoded? No, kmem_cache_shrink later); error: GFP_KERNEL fail (no mem) -> SLAB_PANIC panic("Out of memory for dentry_cache")]
          | | [e.g., dentry_cachep->num_partial = 0; slabp = alloc_slab(cachep); insert in cachep->slabs_full if full; printk none; used in d_alloc (kmem_cache_alloc(dentry_cachep, GFP_KERNEL))]
          |
          +--> Inode Cache Create (Line ~405): kmem_cache_create("inode_cache", sizeof(struct inode), 0, SLAB_RECLAIM_ACCOUNT, NULL); // Cache for inode structs (~320B each); initial ~256 entries
          | |
          | +--> Similar to dentry: cachep = kmem_cache_alloc(...); cachep->name = "inode_cache"; cachep->object_size = sizeof(struct inode); cachep->ctor = inode_init_always; cachep->dtor = destroy_inode; slab_init(cachep);
          | | [Global: extern struct kmem_cache *inode_cachep; [in fs/inode.c; struct inode { struct hlist_node i_hash; struct list_head i_list; struct list_head i_sb_list; struct list_head i_dentry; unsigned long i_ino; atomic_t i_count; umode_t i_mode; unsigned int i_nlink; uid_t i_uid; gid_t i_gid; kdev_t i_rdev; loff_t i_size; struct timespec i_atime, i_mtime, i_ctime; ... }; Rationale: Cache for file metadata (VFS iget); ctor = inode_init_always (sets i_count=1, i_nlink=1, etc.); dtor = destroy_inode (iput final)]
          | | [Rationale: Reuse inodes for open/close; 2.6.20: object_size=320B (timespec x3, list_heads); no SLAB_NO_DEBUG; error: Create fail -> panic via SLAB_RECLAIM_ACCOUNT? No, SLAB_PANIC if flag; fallback no cache (kmalloc slow)]
          | | [e.g., inode_cachep->flags = SLAB_RECLAIM_ACCOUNT; used in alloc_inode (kmem_cache_alloc(inode_cachep, GFP_KERNEL)); iput calls dtor if count=0; 2.6.20: Initial 256 objs (kmem_cache_shrink adjusts)]
          |
          +--> Cache Size Hints (Line ~410): // Hardcoded initial sizes in kmem_cache_create (objs=512 dentry, 256 inode via slab_init default); but vfs_caches_init sets via kmem_cache_shrink later? No, initial in slab_create (min objs=8, grow)
          | |
          | +--> Rationale: Pre-alloc for VFS perf (d_alloc/iget fast); 2.6.20: SLAB grow on demand (kmem_cache_grow); global int dentry_stat[NR_DENTRY]; inode_stat[NR_INODE]; for /proc/slabinfo
          | | [e.g., slab_init: cachep->objs_per_slab = cachep->size / BYTES_PER_WORD +1 (~32 objs/slab); used in proc_slab_read /proc/slabinfo]
          |
          +--> 2.6.20 Specifics: SLAB_RECLAIM_ACCOUNT charges to kmem (reclaim on lowmem); no percpu caches (added SLUB later); dentry_cache align=ARCH_MIN_TASKALIGN=16B; globals exported dentry_cachep/inode_cachep (fs/dcache.c, fs/inode.c); no dcache_init_icache (separate)
          | |
          | +--> [Overall: ~380-390ms (kmem_cache_create ~20ms x2, slab_init ~10ms); silent; ERR: Slab alloc fail -> panic("Unable to create inode_cache"); Rationale: VFS core alloc ready (dentry for path, inode for file); next proc_root_init mount /proc]
    |
    v
Proc Root Init (Phase 27: proc_root_init; ~390-400ms; [C]; mount /proc fs; 2.6.20: Basic /proc, no /proc/kallsyms early)





Proc Root Init (proc_root_init)
[fs/proc/root.c; ~390-400ms; [C]; Line ~50; protected mode C in start_kernel after VFS caches; initializes /proc filesystem root (proc_root); creates symlinks like /proc/self/mounts -> /proc/mounts; mounts procfs at /proc (MS_MNT_DIR for dir); globals: struct proc_dir_entry *proc_root (root /proc), *proc_kcore, *proc_self; 2.6.20: Basic /proc (no kallsyms/symbols early, CONFIG_PROC_KCORE=y opt); state: PE=1 PG=1 WP=1 IRQ=1 masked; post-phase 26 VFS caches (inode_cache ready); next rest_init phase 28; output: None (silent); error: Mount fail -> printk "mount proc failed" continue (no /proc, but boot ok); proc_mkdir alloc fail -> no /proc/root (minor)]
    |
    +--> Proc Root Directory Create (Line ~50): proc_root = proc_mkdir("root", NULL); // Create /proc/root dir entry (parent=NULL = root fs)
          |
          +--> Sub-Func proc_mkdir (fs/proc/generic.c Line ~200-250): struct proc_dir_entry *de = kmem_cache_alloc(proc_inode_cachep, GFP_KERNEL); if (!de) return ERR_PTR(-ENOMEM); de->name = "root"; de->mode = S_IFDIR | S_IRUGO | S_IXUGO (0755); de->nlink = 2; de->size = 0; de->proc_iops = &proc_dir_inode_operations; de->proc_fops = &proc_dir_operations; de->parent = parent (NULL); INIT_LIST_HEAD(&de->subdirs); proc_register(proc_root, de); // Add to parent's subdirs list
          | |
          | +--> Globals: struct proc_dir_entry *proc_root; [in root.c; struct { const char *name; mode_t mode; nlink_t nlink; uid_t uid; gid_t gid; loff_t size; struct inode_operations *proc_iops; struct file_operations *proc_fops; struct proc_dir_entry *parent, *next, *subdirs; void *data; }; Rationale: /proc root for all entries (/proc/cpuinfo, /proc/meminfo); 2.6.20: proc_inode_cachep from phase 26; error: kmem_cache_alloc fail -> ERR_PTR(-ENOMEM) return NULL (no /proc/root, mkdir /proc fails later)]
          | | [Rationale: Build /proc hierarchy (dir for subdirs); nlink=2 (self + parent); proc_dir_inode_operations {lookup=proc_lookup}; proc_dir_operations {readdir=proc_readdir}; e.g., de->uid = 0 (root), gid=0; proc_register adds to proc_root->subdirs]
          | | [e.g., proc_root->name = NULL (root); mode=0755; used in proc_lookup (dir open) -> filldir "cpuinfo" etc.; 2.6.20: No seq_file default (old readdir); printk none]
          |
          +--> Symlinks Create (Line ~55-60): proc_symlink("self", proc_root, "self"); proc_symlink("mounts", proc_root, "self/mounts"); // /proc/self -> current task /proc/PID; /proc/self/mounts -> /proc/mounts
          | |
          | +--> Sub-Func proc_symlink (fs/proc/generic.c Line ~300-350): struct proc_dir_entry *de = kmem_cache_alloc(proc_dir_entry_cachep, GFP_KERNEL); if (!de) return ERR_PTR(-ENOMEM); de->name = "self"; de->mode = S_IFLNK | S_IRWXUGO (0777); de->size = 64; de->data = target ("self"); de->proc_fops = NULL; de->proc_iops = &proc_link_inode_operations; proc_register(parent, de); // Add to subdirs
          | | [Rationale: /proc/self dynamic link to current PID (/proc/self/stat = /proc/1/stat); /proc/mounts legacy compat for /proc/self/mounts; 2.6.20: proc_dir_entry_cache from phase 26; global struct proc_dir_entry *proc_self; error: Alloc fail -> ERR_PTR(-ENOMEM) no symlink (cat /proc/self fail ENOENT)]
          | | [Global: struct inode_operations proc_link_inode_operations = { .readlink = proc_pid_readlink, .follow_link = proc_pid_follow_link }; [readlink returns "self" str; follow_link opens /proc/current_pid; Rationale: Dynamic resolve at open; 2.6.20: No procfs v2; /proc/mounts = show_mounts readdir]
          | | [e.g., de->data = "self" (target str); readlink /proc/self -> "self"; follow_link dget(get_proc_task(current->pid)->dir); 2.6.20: get_proc_task = find_task_by_pid; printk none]
          |
          +--> Procfs Mount (Line ~65): mount("proc", "/proc", "proc", MS_MNT_DIR, ""); // Mount procfs superblock at /proc (dir mount)
          | |
          | +--> Sub-Func mount (fs/namespace.c Line ~2000-2100): struct vfsmount *mnt = do_mount("proc", "/proc", "proc", MS_MNT_DIR, ""); // do_mount: struct file_system_type *fstype = get_fs_type("proc"); struct super_block *sb = fstype->get_sb(fstype, flags, data, mnt); if (IS_ERR(sb)) return sb; mntput(mnt); return mnt;
          | | [Rationale: Activate /proc fs (virtual, in-mem); MS_MNT_DIR = 0x4000 (dir mount); 2.6.20: get_fs_type returns &proc_fs_type; proc_read_super (sb->s_op = &proc_sops; sb->s_root = proc_get_root(sb);); global struct file_system_type proc_fs_type = { .name = "proc", .get_sb = proc_get_sb, .kill_sb = kill_litter_super }; error: get_sb fail (-ENOMEM) -> printk "mount: proc failed" continue (no /proc, but ps/cat fail later)]
          | | [Global: struct super_operations proc_sops = { .alloc_inode = proc_alloc_inode, .destroy_inode = proc_destroy_inode, .write_inode = NULL, .drop_inode = generic_delete_inode, .statfs = proc_statfs }; [proc_get_sb: sb = sget(fstype, NULL, proc_set_super, NULL); if (!sb) return ERR_PTR(-ENOMEM); sb->s_op = &proc_sops; sb->s_root = proc_get_root(sb); sb->s_magic = PROC_SUPER_MAGIC=0x9FA0; mount_single(mnt, sb, data);]
          | | [e.g., /proc mount success "VFS: Mounted proc filesystem"; sb->s_root = proc_root (phase 27); used in open("/proc/cpuinfo", O_RDONLY) -> proc_file_lookup -> proc_read (seq_file); 2.6.20: No procfs v2 (simple readdir); printk "procfs mount ok" none]
          |
          +--> 2.6.20 Specifics: proc_root->nlink=1 (root); no /proc/kallsyms early (CONFIG_KALLSYMS_ALL=n default, added in late_initcall); proc_symlink for compat (/proc/self/mounts = show_mounts); globals proc_root, proc_self (symlink data="self"); no procfs security (selinux later)
          | |
          | +--> [Overall: ~390-400ms (mkdir/sym/mount ~20ms); silent; ERR: Mount -ENOMEM -> "Failed to mount procfs" log continue (/proc missing, dmesg/ps fail); Rationale: /proc for debug (cmdline, meminfo, interrupts); next rest_init fork threads]
    |
    v
rest_init() (Phase 28: main.c end start_kernel; ~400-410ms; [C]; forks kthreadd PID2, kernel_init thread; enables IRQs/preempt; first schedule to idle; 2.6.20: CLONE_FS|CLONE_SIGHAND for kernel_init)






rest_init() (main.c)
[init/main.c; ~400-410ms; [C]; Line ~500; protected mode C at end of start_kernel after proc_root_init; forks kthreadd (PID2 kernel thread daemon for modules/hotplug) and kernel_init (PID1 for do_basic_setup/mount/exec init); enables local IRQs (sti) and preemption (preempt_enable_no_resched); calls first schedule(NULL) to start multitasking (CPU0 -> idle loop); globals: struct task_struct *kthreadd_task, *init_task (PID0); 2.6.20: kernel_thread uses do_fork with CLONE_FS|CLONE_FILES for kthreadd, CLONE_FS|CLONE_SIGHAND for kernel_init (share fs/sig); state: PE=1 PG=1 WP=1 IRQ=1 masked -> IRQ=1 PREEMPT=1; post-phase 27 /proc mount; next idle loop phase 29; output: None (silent fork); error: Fork fail (no mem) -> printk "kernel_thread() failed" continue single thread (no modules, stuck boot)]
    |
    +--> kthreadd Fork (Line ~500): kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES); // Fork PID2 daemon; CLONE_FS share fs_struct (root /), CLONE_FILES share files_struct (fdtab)
          |
          +--> Sub-Func kernel_thread (kernel/fork.c Line ~1000-1100): long clone_flags = CLONE_VM | CLONE_UNTRACED | SIGCHLD; struct pt_regs regs = { .ax = 0, .bx = (unsigned long)fn, .cx = (unsigned long)arg, .ip = (unsigned long)kernel_thread_helper, .sp = (unsigned long)tf }; return do_fork(clone_flags | SIGCHLD, 0, &regs, 0, NULL, NULL); // do_fork creates new task_struct, copy_process, copy_mm, etc.
          | |
          | +--> Global: struct task_struct *kthreadd_task; [in sched.c; kthreadd_task = current after fork; Rationale: Daemon for kthread_create (e.g., modprobe, keventd); 2.6.20: CLONE_UNTRACED no ptrace; do_fork: new = copy_process(clone_flags, stack_start, stack_size, child_tidptr, NULL, &child_tid); if (IS_ERR(new)) return PTR_ERR(new); wake_up_new_task(new);]
          | | [Rationale: Async spawn (schedule runs it); 2.6.20: No CLONE_PARENT_SETTID; kernel_thread_helper ASM stub: movl %ebx, %eax (arg); CALL *%esi (fn(arg)); mov $0x0B, %eax; int $0x80 (exit(0)); error: copy_process -ENOMEM -> "kernel_thread() ENOMEM" return -ENOMEM (continue, no kthreadd, no modules)]
          | | [e.g., fn=kthreadd (loop schedule_timeout_interruptible(MAX_SCHEDULE_TIMEOUT) wait create); arg=NULL; regs.ip = kernel_thread_helper (0xC0108xxx); new PID=2 state=TASK_INTERRUPTIBLE; enqueue_task(&new->run_list, rq=0, ENQUEUE_WAKEUP); printk none]
          |
          +--> kernel_init Fork (Line ~505): kernel_thread(kernel_init, NULL, CLONE_FS | CLONE_SIGHAND); // Fork PID1 init thread; CLONE_FS share fs, CLONE_SIGHAND share sigaction table
          | |
          | +--> Similar to kthreadd: clone_flags = CLONE_VM | CLONE_UNTRACED | CLONE_SIGHAND; regs.bx = (unsigned long)kernel_init; regs.cx = NULL; return do_fork(...); // PID1 (lowest after 0 idle)
          | | [Rationale: Separate thread for basic setup/mount/exec (non-idle); 2.6.20: CLONE_SIGHAND share signals (SIGCHLD etc.); global no specific for kernel_init; error: Fork fail -> "kernel_thread kernel_init failed" (stuck, no mount/init exec)]
          | | [e.g., fn=kernel_init (do_basic_setup, prepare_namespace, exec /sbin/init); arg=NULL; new PID=1 prio=120; enqueue; helper: CALL kernel_init(NULL); exit; 2.6.20: PID1 reparents orphans on exec]
          |
          +--> Preemption Enable No Resched (Line ~510): preempt_enable_no_resched(); // Decrement preempt_count; if ==0 set TIF_NEED_RESCHED=0 but no check; [Rationale: Allow preemption but no immediate resched (safe first schedule); inline current->preempt_count--]
          | |
          | +--> Global: int preempt_count in task_struct; [Rationale: Kernel sections ++, user 0; 2.6.20: No RT; TIF_NEED_RESCHED in thread_info flags; error: Underflow -> bad resched (stuck idle)]
          | | [e.g., preempt_count=0; allow resched in schedule; printk none]
          |
          +--> Local IRQ Enable (Line ~515): local_irq_enable(); // STI asm("sti"); set IF=1 (unmask IRQs); [Rationale: Enable interrupts post-setup (IDT/PIC ready); first timer IRQ0 in idle]
          | |
          | +--> Global: unsigned long irq_flags; [local_irq_save/restore for atomic; 2.6.20: STI direct; Rationale: Start hardware events (timer for jiffies); error: STI bad IDT -> spurious #GP (handlers stub early)]
          | | [e.g., sti; IRQ line 0 (PIT timer) -> do_timer_interrupt -> do_timer(1) jiffies++; ~4ms HZ=250; 2.6.20: PIC vectors 0x20-0x2F]
          |
          +--> First Schedule Call (Line ~520): schedule(NULL); // First multitasking; NULL=prev (no save); picks next task (kthreads or idle)
          | |
          | +--> Sub-Func schedule (kernel/sched.c Line ~1200): if (preempt_count() || !irqs_disabled()) { /* Early safe */ } else { pick_next_task(rq, prev, rf); context_switch(rq, prev, next); } // But early: queues have kthreads, next=kthreadd or kernel_init
          | | [Rationale: Start threads async (idle yields to PID1/2); 2.6.20: O(1) pick highest prio; global struct runqueue *rq = &runqueues[smp_processor_id()]; error: No next -> stuck idle (fork fail earlier)]
          | | [e.g., pick_next_task scans active_array for highest prio (kernel_init prio=120); context_switch ASM switch_to; CPU0 -> kernel_init first]
          |
          +--> 2.6.20 Specifics: CLONE_FS=0x00000200 (share fs_struct), CLONE_FILES=0x00000400 (share files_struct), CLONE_SIGHAND=0x00000800 (share signal_handlers); no CLONE_THREAD (separate pid); kernel_thread returns child PID (2/1); globals current = &init_task pre-fork
          | |
          | +--> [Overall: ~400-410ms (fork ~50ms x2, sti/schedule ~10ms); silent; ERR: Fork -ENOMEM -> printk "fork failed" single thread (no kthreadd/modules, kernel_init runs in start_kernel? No, rest_init returns to idle); Rationale: Multitasking start (threads for setup); next idle loop phase 29]
    |
    v
Parallel: Idle Loop Start (Phase 29: sched.c cpu_idle; ~410ms+; [C/ASM]; CPU0 idle loop hlt; checks need_resched; 2.6.20: default_idle hlt sti cli)





Parallel: Idle Loop Start (cpu_idle() on CPU0)
[init/main.c rest_init -> schedule(); ~400ms+; [C/ASM]; protected mode; post-rest_init first schedule(NULL); CPU0 enters idle process loop (PID0 init_task); hlt for power save; checks need_resched for yield; enables full IRQs/preemption; parallel with kthreadd/kernel_init threads (PID2/1 schedule async); globals: struct task_struct *current (&init_task PID0), struct runqueue *rq (&runqueues[0]), int need_resched (in thread_info flags), unsigned long jiffies (ticks); 2.6.20: O(1) scheduler (prio_array check); default_idle hlt sti cli loop; state: PE=1 PG=1 WP=1 IRQ=0 masked -> IRQ=1 PREEMPT=0 -> PREEMPT=1; from rest_init end; next kthreadd phase 30 (parallel); output: None (silent hlt); error: Infinite loop no resched -> stuck (threads blocked, no init exec panic later); rationale: Steady state kernel (idle ~99% after boot, yields to tasks)]
    |
    +--> First Schedule Call from rest_init (main.c Line ~520): schedule(NULL); // First multitasking call; NULL=prev task (no context save); picks next runnable task (kthreads or idle if none)
          |
          +--> Sub-Func schedule (kernel/sched.c Line ~1200-1500): if (preempt_count() || irqs_disabled()) { /* Early no-preempt safe */ } else { /* Full O(1) */ pick_next_task(rq, prev, rf); context_switch(rq, prev, next); } // But early: queues have kthreads (PID1/2 forked), next = highest prio (kernel_init prio=120 normal)
          | |
          | +--> pick_next_task Sub-Sub (Line ~1250): struct task_struct *next = idle; for (int prio=0; prio < MAX_PRIO; prio++) { if (rq->active->bitmap[prio/BITS_PER_LONG] & (1UL << (prio % BITS_PER_LONG))) { next = list_entry(rq->active->queue[prio % 2].next, struct task_struct, run_list); break; } } // Scan bitmap for highest prio ready queue
          | | [Rationale: O(1) find highest bit in bitmap (ffz for lowest empty? Scan low to high for highest prio=lowest num RT=0 high); 2.6.20: rq->active = &active_rq[0] (single CPU); global struct runqueue runqueues[NR_CPUS=1]; error: Bitmap corrupt -> wrong next (sched deadlock)]
          | | [Global: int MAX_PRIO=140; [in sched.h; prio 0=MAX_RT_PRIO-1=99 highest RT, 100-139 normal; init_task prio=120; list_head run_list in task_struct; Rationale: Enqueue to queue[prio%2] (current/sleep), set bit; dequeue clear bit list_del]
          | | [e.g., kthreads enqueued prio=120; bitmap[3] bit24 (120=3*32+24) set; next = kernel_init; printk "sched: pick_next %s PID %d" if debug]
          |
          +--> Context Switch to Next (Line ~1300): if (prev != next) { prepare_arch_switch(next, prev, rq); switch_to(prev, next, prev); } // ASM switch_to save/restore context
          | |
          | +--> switch_to Macro (include/asm-i386/system.h Line ~200): asm volatile("pushfl\n\tcli\n\tmovl %%esp,%0\n\tmovl %3,%%esp\n\tpopl %1\n\tpopfl\n\t" : "=m" (prev->thread.esp), "=r" (prev), "=&d" (last) : "a" (next->thread.esp) : "memory"); // Save prev ESP (stack ptr), load next ESP; return prev EIP to caller? Wait, returns to next's saved EIP
          | | [Rationale: ASM context save (ESP stack, EIP ret addr); CLI disable IRQ during switch (~us); 2.6.20: Basic (no FPU TS=0, no debug regs); global struct thread_struct { unsigned long esp0; unsigned long eip; unsigned long fs; unsigned long gs; }; error: Bad ESP (corrupt stack) -> #SS seg fault on pop]
          | | [e.g., prev=&init_task (first no prev); next=kernel_init; save init_task.esp=0xC0303FFC; load kernel_init.esp from fork; return to kernel_init code; prepare_arch_switch loads next GS/FS (per-thread)]
          |
    |
    v
Idle Loop Entry (cpu_idle; sched.c Line ~2000; ~400ms+; [C/ASM]; CPU0 idle loop while(!need_resched) default_idle(); hlt power save; sti/irqs enabled pre-loop)
    |
    +--> cpu_idle Entry: asmlinkage void cpu_idle(void) * [Called from schedule when next=&init_task (idle); PID0 state=TASK_RUNNING; prio=120; current = &init_task]
          |
          +--> Preemption Enable No Resched: preempt_enable_no_resched(); // current->preempt_count--; if (preempt_count ==0) clear TIF_NEED_RESCHED but no check; [Rationale: Allow preemption post-switch but no immediate resched (safe for first loop); inline in sched.h]
          | |
          | +--> Global: int preempt_count in task_struct; unsigned long flags in thread_info (TIF_NEED_RESCHED bit9); [Rationale: Kernel ++ for atomic, user 0; 2.6.20: No RT preemption levels; error: Underflow -> bad flag clear (stuck no resched)]
          | | [e.g., preempt_count=0; TIF_NEED_RESCHED=0; printk none; 2.6.20: set_need_resched from timer if slice expire]
          |
          +--> Local IRQ Enable: local_irq_enable(); // asm("sti"); IF=1 unmask IRQs; [Rationale: Start hardware IRQs (timer first for jiffies); PIC/IDT ready post-trap_init]
          | |
          | +--> Global: unsigned long irq_flags; [for local_irq_save("pushfl; cli; pop %0") /restore("push %0; popfl"); Rationale: Atomic sections; 2.6.20: STI direct no save early; error: STI with pending IRQ -> do_IRQ vec (stubs early do nothing)]
          | | [e.g., sti; first IRQ0 PIT timer ~55ms -> do_timer_interrupt (vector 0x20) -> do_timer(1) jiffies++; xtime_update; ~4ms ticks HZ=250]
          |
          +--> Idle Loop Core: while (1) { while (!need_resched()) { default_idle(); } schedule(); } // Yield if resched flag; else hlt save power
          | |
          | +--> need_resched Check: if (test_tsk_thread_flag(current, TIF_NEED_RESCHED)) { set_tsk_thread_flag(current, TIF_POLLING_NRFLAG); } // Inline test_bit(TIF_NEED_RESCHED, current->thread_info->flags)
          | | [Rationale: Poll flag before hlt (avoid IPI resched interrupt); 2.6.20: TIF_NEED_RESCHED set by timer/check_preempt_curr; global int need_resched in thread_info flags (long flags); error: Bad flag test -> missed resched (starvation)]
          | | [e.g., test_bit(9, flags); if set clear TIF_POLLING_NRFLAG bit10 (fast poll); schedule() yields to next (kthreads first)]
          |
          +--> default_idle Sub-Func (sched.c Line ~2100-2150): if (cpu_has_xsave) { /* XSAVE opt */ } else if (pm_idle) { pm_idle(); } else { asm volatile("sti; hlt; cli" : : : "memory"); } // STI enable IRQ, HLT wait, CLI disable post-wake
          | |
          | +--> Global: void (*pm_idle)(void) = default_idle; [in pm.h; CONFIG_CPU_IDLE=n default hlt; Rationale: Power mgmt idle handler; 2.6.20: No MWAIT (Nehalem+); asm sti hlt cli loop for safe (wake on IRQ, re-disable for next hlt)]
          | | [Rationale: HLT low power (wait IRQ wake); sti before (enable during hlt); cli after (disable for check); 2.6.20: No C-states; error: No hlt (old CPU 386) -> busy loop waste ~100% CPU]
          | | [e.g., asm("sti; hlt; cli"); ~1 cycle entry, waits IRQ (timer ~4ms); wake sti cli loop; printk "CPU0 idle %ld ticks" if debug]
          |
          +--> 2.6.20 Specifics: init_task PID0 policy=SCHED_NORMAL (0), static_prio=120 (NICE_TO_PRIO(0)=120); rq = &runqueues[0]; no cpu_idle per-CPU call_ipi_hook (SMP later); hlt no P4 idle halt (opt later)
          | |
          | +--> [Overall: ~400ms+ ongoing (loop ~4ms/tick); silent; ERR: No yield (need_resched bug) -> stuck idle 100% CPU (no threads run, init timeout panic); Rationale: CPU0 steady idle (yields to user/kthreads); parallel kthreadd/kernel_init run first ticks; next kthreadd phase 30 parallel]
    |
    v
kthreadd Daemon (Phase 30: kernel/kthread.c kthreadd; ~410-420ms; [C]; PID2 loop wait kthread_create; spawns keventd etc.; 2.6.20: No kworkers; state: PREEMPT=1 IRQ=1)











kthreadd Daemon (kthread.c)
[kernel/kthread.c; ~410-420ms; [C]; Line ~100-200; protected mode C; kthreadd thread entry (PID2, forked from rest_init kernel_thread); loops in TASK_INTERRUPTIBLE state waiting for kthread_create calls; spawns kernel threads (e.g., keventd for hotplug, pdflush for writeback); globals: struct task_struct *kthreadd_task, wait_queue_head_t kthread_create_wait; 2.6.20: No kworkers (per-CPU workqueues added 2.6.20 but basic); spawns keventd (/sbin/hotplug) and migration threads if SMP; state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; async from idle loop phase 29; next kernel_init phase 31 (parallel); output: None (silent loop); error: Wait queue corrupt -> stuck loop (no new threads, hotplug fail); rationale: Central daemon for dynamic kernel threads (modules, workqueues)]
    |
    +--> Thread Entry: int kthreadd(void *unused) [Line ~100; unused=NULL from kernel_thread; current = kthreadd_task after fork; PID=2 state=TASK_RUNNING prio=120 normal]
          |
          +--> Wait Queue Head Init (Line ~105): init_waitqueue_head(&kthread_create_wait); // Global wait queue for kthread_create callers to sleep until thread ready
          | |
          | +--> Sub-Func init_waitqueue_head (kernel/wait.c Line ~50): struct wait_queue_head *q = &kthreadd_create_wait; spin_lock_init(&q->lock); INIT_LIST_HEAD(&q->task_list); // Empty list for sleepers
          | | [Rationale: kthread_create sleeps on this queue until kthreadd spawns child and wakes; 2.6.20: Basic spinlock wait_queue_head_t {spinlock_t lock; struct list_head task_list;}; global DECLARE_WAIT_QUEUE_HEAD(kthread_create_wait); error: Lock init fail (no mem) -> corrupt wait (deadlock)]
          | | [Global: extern wait_queue_head_t kthread_create_wait; [in kthread.h; used in kthread_create (wait_event(kthread_create_wait, child != NULL)); Rationale: Synchronize create/spawn; 2.6.20: No per-CPU queues (single daemon)]
          | | [e.g., q->lock = {__lock=0}; task_list.next/prev = &task_list; printk none; 2.6.20: wake_up(&kthread_create_wait) after fork in kthreadd]
          |
          +--> Main Loop: for (;;) { set_current_state(TASK_INTERRUPTIBLE); // Allow signal/schedule; schedule(); // Sleep until woken by kthread_create; if (kthread_should_stop()) break; // Exit if stopped (rare early) } [Line ~110-120; infinite loop wait create calls]
          | |
          | +--> set_current_state: current->state = TASK_INTERRUPTIBLE (2); [Rationale: TASK_RUNNING=0, INTERRUPTIBLE=2 sleep ok signal; 2.6.20: Inline in sched.h; global enum task_state {TASK_RUNNING=0, TASK_INTERRUPTIBLE=2, TASK_UNINTERRUPTIBLE=1, __TASK_STOPPED=4, TASK_DEAD=32}; error: Wrong state -> unkillable sleep]
          | | [schedule: if (state != TASK_RUNNING) { /* Sleep */ add_wait_queue_exclusive(&wait, &wait_entry); } else schedule_timeout; // But here sleep on CPU runqueue until wake_up]
          | | [Rationale: kthreadd sleeps most time (wake on kthread_create from anywhere); 2.6.20: schedule_timeout_interruptible(MAX_SCHEDULE_TIMEOUT) for forever sleep; global int kthread_should_stop() = test_bit(KTHREAD_SHOULD_STOP, &current->flags); (set by kthread_stop)]
          | | [e.g., state=2; schedule() yields to idle; wake on kthread_create wait_event; loop ~1us check, sleep ~4ms ticks; printk none]
          |
          +--> kthread_create Wait Handle (Line ~125): while (!list_empty(&kthread_create_list)) { struct kthread_create_info *create = list_entry(kthread_create_list.next, struct kthread_create_info, list); list_del_init(&create->list); struct task_struct *p; struct pt_regs regs = {0}; regs.bx = (unsigned long)create->threadfn; regs.cx = (unsigned long)create->data; regs.ip = (unsigned long)kernel_thread_helper; p = do_fork(CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL); if (IS_ERR(p)) { create->result = PTR_ERR(p); } else { create->result = p; wake_up_process(p); } wake_up(&kthread_create_wait); } // Spawn child for each pending create
          | |
          | +--> Global: struct list_head kthread_create_list; [DECLARE_LIST_HEAD(kthread_create_list); in kthread.c; struct kthread_create_info { struct list_head list; int (*threadfn)(void *); void *data; struct task_struct *result; struct completion *done; }; Rationale: Queue pending creates from kthread_run (add to list, wake kthreadd); 2.6.20: do_fork same as kernel_thread but no SIGCHLD; kernel_thread_helper ASM: CALL *%esi (%ebx arg); mov $0x0B, %eax; int $0x80 (exit)]
          | | [Rationale: kthreadd spawns (e.g., keventd = kthread_run(keventd_main, NULL, "keventd")); kthread_run = kthread_create + wake_up_process; error: Fork -ENOMEM -> create->result = ERR_PTR(-ENOMEM); wake caller with bad result]
          | | [e.g., create->threadfn = keventd_main (loop workqueue); data=NULL; p = do_fork -> PID3 keventd; create->result = p; wake_up_process(p) (state=RUNNING enqueue); wake_up(kthread_create_wait) wakes kthread_create caller; 2.6.20: No kthread_stop early; list_del_init safe remove]
          |
          +--> 2.6.20 Specifics: kthreadd_task = current; (global for kthread_stop find); no kthread_data (per-thread arg); kthread_create_list protected by kthread_mutex spinlock? No, wait_event assumes exclusive; spawns keventd (INIT_WORK for hotplug), pdflush (writeback), migration/1 (SMP load balance)
          | |
          | +--> [Overall: ~410-420ms (fork ~50ms first spawn, loop ~1us); silent; ERR: List corrupt (add/del fail) -> stuck create (hotplug no thread, udev fail); Rationale: Dynamic threads from anywhere (kthread_run); daemon sleeps 99%; next kernel_init parallel phase 31]
    |
    v
kernel_init() Thread Start (Phase 31: main.c; ~420-430ms; [C]; PID1 do_basic_setup mount exec init; 2.6.20: SysV init)










kernel_init() Thread Start (main.c)
[init/main.c; ~420-430ms; [C]; Line ~550; protected mode C; kernel_init thread entry (PID1, forked from rest_init kernel_thread with CLONE_FS|CLONE_SIGHAND); creates /proc/cmdline; calls do_basic_setup (drivers/fs initcalls); prepare_namespace (mount rootfs /proc /sys); sets system_state=SYSTEM_RUNNING; calls init_post (free initmem, mark rodata RO, exec /sbin/init); globals: enum system_states system_state, unsigned long root_mountflags, struct proc_dir_entry *proc_cmdline; 2.6.20: SysV init exec (/etc/inittab runlevel 3); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; async parallel from idle phase 29; next do_basic_setup phase 32; output: Printk "Kernel command line: ..." for /proc/cmdline; error: No root mount -> panic "VFS: Unable to mount root fs on unknown-block(%d,%d)"; thread do_exit(0) after exec handover]
    |
    +--> Thread Entry: int kernel_init(void *unused) [Line ~550; unused=NULL from kernel_thread; current = &init_task clone PID=1 state=TASK_RUNNING prio=120; runs in kernel space ring0; no return (exec replaces)]
          |
          +--> Proc Cmdline Entry Create (Line ~555): create_proc_read_entry("cmdline", S_IFREG | S_IRUGO, NULL, &cmdline_proc_ops, NULL); // /proc/cmdline exposes boot_command_line str (read-only)
          | |
          | +--> Sub-Func create_proc_read_entry (fs/proc/generic.c Line ~100-150): struct proc_dir_entry *pde = kmem_cache_alloc(proc_dir_entry_cachep, GFP_KERNEL); if (!pde) { printk(KERN_ERR "proc: kmem_cache_alloc failed\n"); return NULL; } pde->name = "cmdline"; pde->mode = S_IFREG | S_IRUGO (0444 read all); pde->nlink = 1; pde->uid = 0; pde->gid = 0; pde->size = 0; pde->proc_fops = &cmdline_proc_fops; pde->proc_iops = NULL; pde->data = NULL; pde->parent = NULL (root /proc); INIT_LIST_HEAD(&pde->subdirs); proc_register(&proc_root, pde); // Add to proc_root->subdirs list
          | | [Rationale: User-space access to cmdline (cat /proc/cmdline = "root=/dev/sda1 ro ..."); 2.6.20: Old proc_fops (read_proc fn); global struct proc_dir_entry *proc_cmdline (assigned pde); error: kmem_cache_alloc -ENOMEM -> printk ERR return NULL (no /proc/cmdline, but cat fails ENOENT, boot ok)]
          | | [Global: struct file_operations cmdline_proc_fops = { .read = cmdline_read_proc, .llseek = generic_read_dir }; int cmdline_read_proc (char *page, char **start, off_t off, int count, int *eof, void *data) { *eof = 1; return snprintf(page, count, "%s\n", boot_command_line); }; [Rationale: Simple read dumps str + \n; off=0 only; no write; used in seq_read for /proc; 2.6.20: No seq_file default (old read_proc); boot_command_line global char *]
          | | [e.g., pde->proc_fops->read = cmdline_read_proc (snprintf "%s\n" boot_command_line ~100B); proc_register inserts sorted by name; /proc/cmdline size=0 (dynamic); printk none]
          |
          +--> Basic Setup Call (Line ~560): do_basic_setup(); // Init cpuset/usermodehelper/bdi/driver; do_initcalls (postcore to device levels ~250 calls for subsys/fs/drivers)
          | |
          | +--> Sub-Func do_basic_setup (main.c Line ~600-700): cpuset_init_smp(); usermodehelper_init(); bdi_init(&noop_backing_dev_info); driver_init(); do_initcalls(); // cpuset: root_cpuset cpus_allowed=~0UL; usermodehelper: uevent_helper="/sbin/hotplug"; bdi: noop_bdi capabilities=BDI_CAP_NO_WRITEBACK; driver_init: bus_register(pci_bus_type, ide_bus_type, etc.); do_initcalls: for(level=0;level<7;level++) for(call=levels[level];call<levels[level+1];call++) err=(*call)(); if(err<0) printk("initcall %p returned %d\n",*call,err);
          | | [Rationale: Prep cgroups/helpers/writeback/buses; initcalls modular (e.g., postcore: rcu_init_node; subsys: pci_init; fs: ext3_init; device: ide_probe_module); 2.6.20: ~250 calls (~1ms each); global initcall_t __initcall_start[], __initcall_end[]; error: Err -ENOMEM log continue (e.g., no pci_scan_bus if mem low)]
          | | [Global: struct bus_type pci_bus_type = { .name = "pci", .match = pci_bus_match, .uevent = pci_uevent, .probe = NULL, .remove = NULL }; [in drivers/pci/bus.c; bus_register adds /sys/bus/pci; 2.6.20: No platform bus early; do_initcalls runs __initcalls sections (linker .initcall1.init etc.)]
          | | [e.g., cpuset_init_smp: root_cpuset.flags=1; usermodehelper_init: modprobe_path="/sbin/modprobe"; driver_init: bus_register(&ide_bus_type) /sys/bus/ide; do_initcalls level1: kmem_cache_init_constructors(); level4 subsys: pci_init (pci_scan_bus(0)); printk "PCI: ..."]
          |
          +--> Namespace Prepare Call (Line ~565): prepare_namespace(); // If initrd pivot_root("/initrd" -> real root); mount_block_root("/dev/root", root_fstype); mount /proc /sys devpts tmpfs /dev/shm
          | |
          | +--> Sub-Func prepare_namespace (init/do_mounts.c Line ~800-900): if (saved_root_name[0]) { /* From cmdline root= */ } if (initrd_start) { sys_mount(".", "/", NULL, MS_MOVE, NULL); sys_chroot("/initrd"); root_mountflags |= MS_RDONLY; pivot_root(".", "/initrd/initrd"); sys_chroot("."); } mount_block_root("/dev/root", root_fstype); mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL); mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC, NULL); mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "newinstance,ptmxmode=0000"); mount("tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
          | | [Rationale: Mount real root (ext3 from root=/dev/sda1); pivot if initrd (move / to /initrd, new root /dev/sda1); standard mounts (/proc for debug, /sys sysfs, /dev pts for ttys, shm tmpfs); 2.6.20: No devtmpfs (ramfs /dev static); global char saved_root_name[64] from parse (root=/dev/sda1 -> "sda1"), root_fstype="ext3" from rootfstype=; error: Mount -ENODEV -> "VFS: Cannot open root device" panic; pivot fail -> "No init found in initrd" panic]
          | | [Global: unsigned long initrd_start, initrd_end; [from boot_params.ramdisk_image/size; Rationale: Pivot swaps initrd tmp root to real; mount_block_root: get_sb_bdev(root_fstype, root_dev=MKDEV(3,1), flags); read_super via fs type get_sb; submit_bh(READ) for superblock I/O]
          | | [e.g., initrd_start=0x2000000: sys_mount("/initrd", "/", NULL, MS_MOVE, NULL); pivot_root(".", "/initrd/initrd"); mount("ext3", "/dev/root", "ext3", MS_RDONLY, NULL); success "VFS: Mounted root (ext3 filesystem) on device 3:1"; 2.6.20: No overlayfs; ramfs /dev (mknod /dev/console)]
          |
          +--> System State to Running (Line ~570): system_state = SYSTEM_RUNNING; wake_up(&system_wq); // From BOOTING to RUNNING; signal waiting threads (e.g., reboot paths)
          | |
          | +--> Global: enum system_states system_state; [in linux/kernel.h; SYSTEM_BOOTING=0, SYSTEM_RUNNING=1, SYSTEM_HALT=2, SYSTEM_POWER_OFF=3, SYSTEM_RESTART=4; Rationale: Gate user helpers (usermodehelper check RUNNING); 2.6.20: wake_up wakes wait_queue_head system_wq (empty early); error: Wrong set -> helpers fail (modprobe stuck)]
          | | [e.g., system_state=1; printk "Kernel panic - not syncing: No working init found" if no exec; wake_up_all(&system_wq); 2.6.20: system_wq = DECLARE_WAIT_QUEUE_HEAD(system_wq); used in kernel_restart etc.]
          |
          +--> Proc Caches Additional Init (Line ~575): proc_caches_init(); // kmem_cache_create("proc_inode_cache", sizeof(struct proc_inode), 0, SLAB_HWCACHE_ALIGN, NULL); for /proc inodes/dirs
          | |
          | +--> Sub-Func proc_caches_init (fs/proc/proc.c Line ~50-100): kmem_cache_create("proc_inode_cache", sizeof(struct proc_inode), 0, SLAB_HWCACHE_ALIGN | SLAB_RECLAIM_ACCOUNT, NULL); kmem_cache_create("proc_dir_entry_cache", sizeof(struct proc_dir_entry), 0, SLAB_PANIC, NULL); // SLAB_PANIC = panic on alloc fail
          | | [Rationale: Slab for /proc efficiency (proc_alloc_inode etc.); 2.6.20: proc_inode_cache ~256B (dentry-like), proc_dir_entry_cache ~128B; global struct kmem_cache *proc_inode_cachep, *proc_dir_entry_cachep; error: Create fail -> SLAB_PANIC "Out of memory" (boot fail)]
          | | [Global: struct super_operations proc_sops = { .alloc_inode = proc_alloc_inode (kmem_cache_alloc(proc_inode_cachep, GFP_KERNEL)), .destroy_inode = proc_destroy_inode (kmem_cache_free), .drop_inode = generic_delete_inode }; [Rationale: /proc inodes from cache; 2.6.20: No percpu; used in proc_get_inode]
          | | [e.g., proc_inode_cachep->object_size = sizeof(struct proc_inode) ~256B; proc_dir_entry_cachep flags=SLAB_PANIC; printk none]
          |
          +--> Post Call (Line ~580): init_post(); // free_initmem (slab_free __init sections ~2-4MB), mark_rodata_ro (set_memory_ro rodata), proc_sys_init (/proc/sys mount), run_init_process("/sbin/init")
          | |
          | +--> Sub-Func init_post (main.c Line ~900-950): free_initmem(); // Walk __init_begin to __init_end, slab_free sections (text/data/bss __init); mark_rodata_ro(); // set_memory_ro((unsigned long)__rodata_start, (unsigned long)__rodata_end - __rodata_start); proc_sys_init(); run_init_process("/sbin/init"); // do_execve("/sbin/init", argv={"/sbin/init"}, envp); fallback /etc/init /bin/sh -> panic "No init found"
          | | [Rationale: Cleanup boot mem (reclaim 2-4MB), protect rodata (CR0.WP); /proc/sys for tunables; handover to user PID1; 2.6.20: free_initmem loops mem_map[pfn] if PageSlab && slab in __init, __free_pages; mark_rodata_ro clears WP temp, set PTE RO, WP=1; global unsigned long __init_begin/end; error: Exec fail -> panic "Kernel panic - not syncing: No init found. Try passing init= option"]
          | | [Global: extern void (*initcall0_start/end) (void); [linker .initcall0; but init_post not initcall; run_init_process: char *argv[] = {"/sbin/init", NULL}; do_execve("/sbin/init", argv, current->envp); 2.6.20: SysV init reads /etc/inittab; envp from init_task]
          | | [e.g., free_initmem: for (pfn = __pa(__init_begin) >> PAGE_SHIFT; pfn < __pa(__init_end) >> PAGE_SHIFT; pfn++) if (PageSlab(mem_map[pfn])) kfree(mem_map[pfn].private); printk "Freeing unused kernel memory: %dk freed"; mark_rodata_ro: local_save_cr0; cr0 &= ~0x10000; write_cr0(cr0); set_memory_ro(__rodata_start, num_pages); write_cr0(local_restore_cr0); run_init_process exec ELF /sbin/init PID1; fallback chain /etc/init /bin/init /bin/sh]
          |
          +--> Thread Exit (Line ~585): return 0; // do_exit(0); schedule to idle or next thread; [Rationale: Handover complete, thread ends (exec replaces image)]
          | |
          | +--> Sub-Func do_exit (kernel/exit.c Line ~100-200): current->flags |= PF_EXITING; exit_signals(current); exit_notify(); exit_mm(); exit_files(); exit_fs(); exit_thread(); ... schedule(); // Never returns, schedules next
          | | [Rationale: Cleanup thread (signals, mm, files, fs); 2.6.20: No cgroup exit; global int pidhash_lock; error: Exit stuck (refcount leak) -> zombie (reaper init reaps)]
          | | [e.g., do_exit(0) -> current->exit_code=0; schedule_tail -> finish_task_switch -> context to next; 2.6.20: PID1 special (reparents children)]
          |
          +--> 2.6.20 Specifics: No cgroup v1 full (basic cpusets in do_basic_setup); uevent_helper exec /sbin/hotplug for udev; noop_bdi for tmpfs no dirty track; initcalls strict levels (no deferred probe, -EPROBE_DEFER not used); globals exported system_state (module param sysctl)
          | |
          | +--> [Overall: ~420-430ms (do_basic_setup ~300ms, mount ~50ms, exec ~20ms); output: "VFS: Mounted root (ext3) on device 3:1" / "Freeing unused kernel memory: 232k freed"; ERR: Mount -EINVAL -> panic "VFS: Unable to mount root fs"; Rationale: Final setup before user handover (drivers probed, root mounted, /proc/sys ready); next init_post exec phase 38]
    |
    v
Initcalls Execution (Phase 33: do_initcalls in do_basic_setup; ~500-550ms; [C]; core/postcore levels; 2.6.20: ~250 total, level0 core page_launder_init)







do_basic_setup() (main.c)
[init/main.c; ~430-500ms; [C]; Line ~600-700; protected mode C in kernel_init thread PID1; initializes basic subsystems in dep order: cpuset for cgroup CPU/mem limits, usermodehelper for kernel->user exec (modprobe/hotplug), bdi for writeback balancing, driver_init for bus registration (PCI/IDE/SCSI), do_initcalls for modular init (postcore to device levels ~250 calls); globals: struct cpuset root_cpuset, char *uevent_helper ("/sbin/hotplug"), struct backing_dev_info noop_backing_dev_info, struct bus_type pci_bus_type/ide_bus_type; 2.6.20: No device-mapper crypt early (subsys_initcall dm_crypt_init later if CONFIG_DM_CRYPT=y); basic noop_bdi capabilities=BDI_CAP_NO_ACCT_DIRTY (no dirty bytes track for tmpfs); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; post-phase 31 kernel_init entry; next prepare_namespace phase 36; output: Printk "cpuset: root set all CPUs/mems", "usermodehelper: uevent_helper='/sbin/hotplug'", "driver core: built-in: ide-pci (vendor=8086 device=1234)", initcall returns "initcall pci_init returned 0"; error: Initcall -ENOMEM -> printk "initcall %p returned -12" continue (no panic unless SLAB_PANIC flag); bus_register fail -> no /sys/bus/ide (udev partial)]
    |
    +--> CPUs et Init SMP (Line ~600): cpuset_init_smp(); // Early cgroup cpuset prep for SMP affinity; sets root_cpuset allowed all CPUs/mems
          |
          +--> Sub-Func cpuset_init_smp (kernel/cpuset.c Line ~100-150): struct cpuset *cs = &root_cpuset; cs->cpus_allowed = CPU_MASK_ALL; cs->mems_allowed = MEM_MASK_ALL; cs->flags = CS_ONLINE; cs->partition_root = cs; INIT_LIST_HEAD(&cs->css.cgroup->children); INIT_LIST_HEAD(&cs->css.cgroup->sibling); cs->css.cgroup->parent = NULL; // Root cgroup full access, no parent
          | |
          | +--> Globals: struct cpuset root_cpuset; [in cpuset.c; struct cpuset { struct cgroup_subsys_state css; cpumask_t cpus_allowed; nodemask_t mems_allowed; unsigned long flags; struct cpuset *parent; struct list_head children; struct list_head sibling; struct cpuset *partition_root; }; Rationale: Limit tasks to CPUs/mems (sched_setaffinity); 2.6.20: CONFIG_CPUSETS=y opt, basic cgroups (cpuset_subsys_id=0); CPU_MASK_ALL = {bits=~0UL for NR_CPUS=32}; error: List head init fail (no mem) -> corrupt hierarchy (affinity wrong)]
          | | [Rationale: Early for smp_prepare_cpus (limit APs to cpuset); cs->partition_root = self (root partition); 2.6.20: No v1 full cgroups (cpuset only subsys); printk "cpuset: root set all CPUs/mems" if debug]
          | | [e.g., cpus_allowed.bits[0]=0xFFFFFFFF (all 32 CPUs); mems_allowed.nodes[0]=1 (node0); used in cpuset_attach_task; 2.6.20: No cpuset_migrate_mm (later)]
          |
          +--> Usermode Helper Init (Line ~605): usermodehelper_init(); // Setup kernel exec helpers for modprobe/uevent; defaults /sbin/modprobe /sbin/hotplug; registers /proc/sys/kernel/uevent_helper
          | |
          | +--> Sub-Func usermodehelper_init (kernel/kmod.c Line ~100-150): uevent_helper = "/sbin/udev"; modprobe_path = "/sbin/modprobe"; // Defaults; create_proc_read_entry("uevent_helper", 0644, &proc_kern_root, uevent_helper_read, NULL); create_proc_write_entry("uevent_helper", 0222, &proc_kern_root, uevent_helper_write, NULL); // /proc/sys/kernel/uevent_helper r/w
          | | [Rationale: Kernel calls user-space for events (request_module("ext3") exec modprobe, uevent for udev device add); 2.6.20: /proc write uevent_helper sets path, execve("/sbin/udev", argv, envp); global char *uevent_helper, *modprobe_path; error: Proc create fail (-ENOMEM) -> printk "usermodehelper: proc failed" (default path used, but no tune)]
          | | [Global: struct file_operations uevent_helper_fops = { .read = uevent_helper_read, .write = uevent_helper_write }; int uevent_helper_read (char *page, char **start, off_t off, int count, int *eof, void *data) { *eof = 1; return snprintf(page, count, "%s\n", uevent_helper ? uevent_helper : ""); }; int uevent_helper_write (struct file *file, const char *buffer, unsigned long count, void *data) { char *path = get_uevent_helper(buffer, count); if (path) uevent_helper = path; return count; }; [Rationale: Tune uevent_helper from user; get_uevent_helper memdup_user buffer; 2.6.20: No systemd, SysV /sbin/udev or hotplug; call_usermodehelper("/sbin/modprobe", {"modprobe", "-s", "-q", "--", "ext3"}, UMH_WAIT_EXEC) for load]
          | | [e.g., uevent_helper = "/sbin/udev"; modprobe_path = "/sbin/modprobe"; /proc/sys/kernel/uevent_helper read " /sbin/udev\n"; write "systemd-udevd" sets path; 2.6.20: UMH_WAIT_EXEC=0x4 (wait exec, not proc)]
          |
          +--> Backing Dev Info Init (Line ~610): bdi_init(&noop_backing_dev_info); // Init noop BDI for non-block fs (tmpfs/ramfs/proc) no writeback/dirty account
          | |
          | +--> Sub-Func bdi_init (mm/backing-dev.c Line ~50-100): struct backing_dev_info *bdi = &noop_backing_dev_info; bdi->name = "noop"; bdi->ra_pages = 0; bdi->capabilities = BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK | BDI_CAP_MAP_COPY; init_timer(&bdi->wb.wb_wait); bdi->wb.dirty_exceeded = 0; bdi->wb.dirty_sleep = 0; // No dirty tracking/timer
          | | [Rationale: BDI for balance_dirty_pages (throttle writes); noop for virtual fs (no cache flush); 2.6.20: wb = writeback struct {struct bdi_writeback wb; timer_list wb_wait;}; global struct backing_dev_info noop_backing_dev_info; error: Timer init fail (no mem) -> no wb_wait (dirty stall rare)]
          | | [Global: enum bdi_cap { BDI_CAP_NO_ACCT_DIRTY = 1<<0, BDI_CAP_NO_WRITEBACK = 1<<1, BDI_CAP_MAP_COPY = 1<<2, BDI_CAP_MAP_DIRECT = 1<<3 }; [Rationale: Flags for fs writepage (tmpfs uses noop); bdi_register(&noop_bdi, "char"); used in page_alloc GFP_NOFS; 2.6.20: No cfq bdi (deadline elevator)]
          | | [e.g., noop_bdi.ra_pages=0 (no read-ahead); wb_wait timer for dirty_throttle; printk "bdi-noop registered" none; 2.6.20: Used for /proc /sys no dirty]
          |
          +--> Driver Init (Line ~615): driver_init(); // Register core buses (PCI, platform, USB, IDE, SCSI) via bus_register; scans /sys/bus
          | |
          | +--> Sub-Func driver_init (drivers/base/init.c Line ~50-100): extern struct bus_type pci_bus_type, platform_bus_type, usb_bus_type; bus_register(&pci_bus_type); bus_register(&platform_bus_type); bus_register(&usb_bus_type); bus_register(&ide_bus_type); // Etc. for all built-in (CONFIG_BLK_DEV_IDE=y)
          | | [Rationale: Setup /sys/bus hierarchy for device probe; bus_register: kset_register(&bus->p->kobj, &device_kset); sysfs_create_dir(&bus->kobj, /sys/bus); 2.6.20: ~10 buses (pci name="pci", match=pci_bus_match, uevent=pci_uevent); global struct kset *bus_kset; error: Kset register fail (-ENOMEM) -> no /sys/bus/pci (probe ok, udev partial)]
          | | [Global: struct bus_type ide_bus_type = { .name = "ide", .match = ide_bus_match (media enum), .probe = ide_bus_probe, .remove = ide_bus_remove, .uevent = ide_uevent }; [in drivers/ide/ide.c; bus_register adds /sys/bus/ide/devices/hda; 2.6.20: uevent = kobject_uevent_env (env "MODALIAS=ide:hda"); printk "bus: %s: registered" if debug]
          | | [e.g., pci_bus_type.name="pci"; bus_register -> /sys/bus/pci; driver_register(pci_compat_driver) for legacy; 2.6.20: No amba bus (ARM); error: Duplicate register -> overwrite (wrong match)]
          |
          +--> Initcalls Run (Line ~620): do_initcalls(); // Execute all __initcalls by level (pure=0 early mem to late=6 calibrate); ~250 calls total
          | |
          | +--> Sub-Func do_initcalls (main.c Line ~850-900): static const initcall_t *initcall_levels[] = { &__initcall0_start, &__initcall1_start, &__initcall2_start, &__initcall3_start, &__initcall4_start, &__initcall5_start, &__initcall6_start, &__initcall7_end }; for (int level=0; level<ARRAY_SIZE(initcall_levels)-1; level++) { for (initcall_t *call = initcall_levels[level]; call < initcall_levels[level+1]; call++) { int err = do_one_initcall(*call); if (err < 0) printk(KERN_CRIT "do_basic_setup: %p returned %d\n", *call, err); } }
          | | [Rationale: Dep-ordered init (pure: page_launder_init mem reclaim; core: kmem_cache_init slab; postcore: rcu_init_node; arch: smp_init; subsys: pci_init; fs: ext3_init; device: ide_disk_init; late: calibrate_delay); 2.6.20: Linker sections .initcallN.init (e.g., __initcall2_start = net_dev_init); do_one_initcall: trace_initcall_start(*call); err = (*call)(); trace_finishcall; if (err) printk; global extern initcall_t __initcallN_start/end[]; error: Fn NULL -> "No initcall fn" log; -ENOMEM common log continue]
          | | [Global: struct trace_initcall { initcall_t fn; int err; }; [in trace/events/initcall.h; but 2.6.20 no tracepoints (CONFIG_FTRACE=n default); Rationale: Modular probe (drivers auto); ~250 calls ~200ms; 2.6.20: No deferred (fail continue, EPROBE_DEFER not used)]
          | | [e.g., level0 pure: mem_init(); level1 core: kmem_cache_init(); level2 postcore: rcu_init_node(); level3 arch: smp_init(); level4 subsys: pci_init (pci_scan_bus_parent(0, &pci_bus, NULL)); level5 fs: register_filesystem(&ext3_fs_type); level6 device: ide_probe_module (probe hwifs); printk "initcall ext3_init returned 0" if err=0 none]
          |
          +--> 2.6.20 Specifics: cpuset_subsys.early_init=1 (cpuset_init before smp); uevent_helper r/w /proc/sys/kernel/uevent_helper (get_uevent_helper memdup_user); noop_bdi.ra_pages=0 no read-ahead; driver_init bus_register ~10 buses (/sys/bus/pci/ide/platform); initcalls strict (postcore rcu/timer, arch smp/pci, subsys net/ide, fs ext3, device blk/ide_disk, late calibrate); no blk-mq (legacy ll_rw_blk)
          | |
          | +--> [Overall: ~430-500ms (initcalls ~300ms dominant, bus ~20ms); output: "bus: 'pci': registered" /sys/bus; ERR: Alloc fail in initcall -> -12 log continue (e.g., no ext3 register, mount fail later); Rationale: Subsys ready (buses for device probe, helpers for .ko load, caches for VFS, cgroups for affinity); next prepare_namespace mount root]
    |
    v
prepare_namespace() (Phase 36: init.c/do_mounts.c; ~750-850ms; [C]; pivot initrd if present; mount root /proc /sys devpts tmpfs; 2.6.20: Ramfs /dev static no devtmpfs)











Initcalls: Core/Postcore (do_initcalls)
[init/main.c; ~500-550ms; [C]; Line ~300; protected mode C in do_basic_setup; executes early initcall levels 0-1 (pure/core/postcore ~50 calls); modular init by dep order (pure: mem reclaim, core: slab alloc, postcore: rcu/timer); globals: initcall_t *initcall_levels[7] (pointers to linker sections), __initcall_start/end[7] (array of fn ptrs); 2.6.20: ~50 calls in levels 0-1 (~1ms each); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; post-driver_init phase 32; next arch/subsys phase 34; output: Printk "initcall %p returned %d" if err<0 (e.g., -ENOMEM); error: Fn NULL -> "No initcall fn" log continue; panic if SLAB_PANIC in cache create]
    |
    +--> Levels Array Setup (Line ~300): static const initcall_t *initcall_levels[] __used = { &__initcall0_start, &__initcall1_start, &__initcall2_start, &__initcall3_start, &__initcall4_start, &__initcall5_start, &__initcall6_start, &__initcall_end }; // 7 levels + end
          |
          +--> Linker Sections: __initcallN_start/end = array of initcall_t (fn ptrs) in .initcallN.init (e.g., __initcall0_start = page_launder_init; __initcall1_start = kmem_cache_init); [Rationale: Compiler __attribute__((section(".initcall0.init"))) for pure_initcall(fn); linker sorts by section; 2.6.20: 7 levels (0 pure early, 1 core slab, 2 postcore rcu, 3 arch smp, 4 subsys pci, 5 fs ext3, 6 device ide_disk); global extern initcall_t __initcall_start[], __initcall_end[]; error: Bad linker (overlapping sections) -> wrong order (dep crash)]
          | |
          | +--> Global: typedef int (*initcall_t)(void); [in init.h; fn return 0 success, -err fail; Rationale: Modular probe (drivers auto); 2.6.20: ~250 total calls, levels 0-1 ~50 (mem/slab/rcu)]
          | | [e.g., level0 pure: page_launder_init (mem reclaim); level1 core: kmem_cache_init (slab alloc); used in do_initcalls for_each_level]
          |
          +--> Level Loop Start (Line ~305): for (int level=0; level < ARRAY_SIZE(initcall_levels) - 1; level++) { // Loop 0-6; const initcall_t *start = initcall_levels[level]; const initcall_t *end = initcall_levels[level+1];
          | |
          | +--> Rationale: Dep order (pure before core, postcore before arch); 2.6.20: ARRAY_SIZE=8 (7+end); global const initcall_t * const initcall_levels[ARRAY_SIZE]; error: Bad array -> segfault on deref (corrupt linker)]
          |
          +--> Call Loop Per Level (Line ~310): for (initcall_t *call = start; call < end; call++) { int err = do_one_initcall(*call); if (err < 0) printk(KERN_CRIT "do_initcalls: %p returned %d\n", *call, err); } // Execute each fn
          | |
          | +--> Sub-Func do_one_initcall (internal Line ~850-900): if (!(*call)) { printk(KERN_WARNING "do_one_initcall: No initcall fn\n"); return -EINVAL; } int err = (*call)(); // Call fn(); if (err) printk(KERN_CRIT "initcall %p returned %d\n", *call, err); return err;
          | | [Rationale: Run fn (e.g., kmem_cache_init() alloc slab); trace if CONFIG_TRACE (no in 2.6.20); 2.6.20: KERN_CRIT=3 (critical); global no trace; error: Fn segfault -> #PF panic; -ENOMEM common (log continue)]
          | | [Global: extern int initcall_debug; [module param; if=1 trace each call time; 2.6.20: No default, off; printk "calling %p @ %p\n" fn, call if debug]
          | | [e.g., call = &kmem_cache_init; err = kmem_cache_init() (0); no print; level1 ~20 calls (slab, rcu, softirq); total levels 0-1 ~50 calls ~50ms]
          |
          +--> Core Level Examples (Level 0 Pure ~Line initcall0): pure_initcall(mem_init); // mm/mem.c init mem zones (early after zonelists)
          | |
          | +--> mem_init Fn (mm/mem.c Line ~100): printk("Memory: %luk/%luk available (%luk kernel code, %luk reserved, %luk data, %luk bss)\n", nr_free_pages() << (PAGE_SHIFT-10), max_pfn << (PAGE_SHIFT-10), codepages, reservedpages, datapages, bss pages); // Calc free/resvd from mem_map _count
          | | [Rationale: Early mem stats post-page count; 2.6.20: nr_free_pages() sum free zones; global unsigned long high_memory = (max_low_pfn << PAGE_SHIFT); error: Bad mem_map -> wrong stats (leak)]
          | | [e.g., "Memory: 1024000k/1048576k available (2816k kernel code, 12352k reserved, 1024k data, 256k bss)"; 2.6.20: codepages = (__init_end - __init_begin + ...)/PAGE_SIZE]
          |
          +--> Postcore Level Examples (Level 2 Postcore ~Line initcall2): postcore_initcall(rcu_init_node); // rcu.c init RCU tree node (early single)
          | |
          | +--> rcu_init_node Fn (rcu.c Line ~400): rcu_alloc_percpu_data(); // Per-CPU rcu_data for callbacks; [Rationale: Postcore after slab; 2.6.20: Classic RCU alloc rdp; error: Alloc fail -> RCU off]
          | | [Global: struct rcu_ctrlblk rcu_ctrlblk; [init rcp->completed=0; used in call_rcu; 2.6.20: No tree, single gp]
          |
          +--> 2.6.20 Specifics: Levels: 0 pure (mem), 1 core (slab), 2 postcore (rcu/timer/softirq), 3 arch (smp/pci), 4 subsys (net/ide), 5 fs (ext3), 6 device (blk/ide_disk), 7 late (calibrate); no trace_initcall (CONFIG_FTRACE=n); __used to avoid linker discard
          | |
          | +--> [Overall: ~500-550ms (50 calls ~50ms); output: "initcall mem_init returned 0" none (only err); ERR: Slab panic in cache create -> "Out of memory"; Rationale: Dep init (mem before slab, postcore after sched); next arch/subsys levels 34]
    |
    v
Initcalls: Arch/Subsys/FS (Phase 34: do_initcalls cont; ~550-650ms; [C]; arch smp_init, subsys pci_init, fs ext3_init; 2.6.20: Levels 3-5 ~100 calls)












Initcalls: Arch/Subsys/FS (do_initcalls cont)
[init/main.c; ~550-650ms; [C]; Line ~300; protected mode C continuation of do_initcalls in do_basic_setup; executes mid-level initcalls 3-5 (arch/subsys/fs ~100 calls); modular dep-ordered init (arch: smp_init for CPUs, subsys: pci_init for bus scan, fs: ext3_init for filesystem register); globals: initcall_t *initcall_levels[7] (pointers to linker .initcallN sections), struct pci_bus *pci_bus_root (bus 0), struct file_system_type ext3_fs_type; 2.6.20: Levels 3-5 ~100 calls (~1ms each); strict order (no deferred probe, fail continue); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; post-core/postcore phase 33; next device/late phase 35; output: Printk "SMP: Booting %d CPUs" for smp_init, "PCI: Using configuration type 1" for pci_init, "EXT3 FS on sda1" for ext3; error: Initcall -ENOMEM -> printk "initcall %p returned -12" continue (e.g., no pci_scan_bus if mem low, no ext3 register mount fail later)]
    |
    +--> Level Loop Continuation (Line ~310): for (int level=2; level < 5; level++) { // Cont from core/postcore; levels 2 postcore (rcu/timer), 3 arch (smp/pci early), 4 subsys (net/ide), 5 fs (ext3)
          |
          +--> Call Loop: for (initcall_t *call = initcall_levels[level]; call < initcall_levels[level+1]; call++) { int err = do_one_initcall(*call); if (err < 0) printk(KERN_CRIT "%s: %p returned %d\n", __func__, *call, err); } // do_one_initcall calls fn(); trace if debug
          | |
          | +--> do_one_initcall Detail (main.c Line ~850): if (!*call) printk(KERN_WARNING "do_one_initcall: No fn\n"); else { int err = (*call)(); if (err) printk(KERN_CRIT "initcall %p returned %d\n", *call, err); return err; } // Simple call, log err
          | | [Rationale: Run fn (e.g., smp_init()); 2.6.20: No trace_initcall (CONFIG_FTRACE=n); global no; error: Fn crash -> #PF panic; -EPROBE_DEFER not used (strict)]
          | | [Global: initcall_t __initcall3_start/end[] linker .initcall3.arch (e.g., smp_init); Rationale: Dep (arch after postcore sched, subsys after arch pci, fs after subsys drivers); ~100 calls levels 3-5 ~150ms]
          |
          +--> Arch Level Examples (Level 3 Arch ~Line initcall3): arch_initcall(smp_init); // kernel/smpboot.c init SMP (boot APs if >1 CPU)
          | |
          | +--> smp_init Fn (arch/i386/kernel/smpboot.c Line ~100-200): if (smp_num_cpus > 1) { smp_boot_cpus(); } else smp_num_cpus = 1; printk(KERN_INFO "SMP: Booting %d CPUs\n", smp_num_cpus); // Early stub, full in smp_prepare_cpus
          | | [Rationale: Prep SMP (APIC/MADT parse); 2.6.20: CONFIG_SMP=y opt; global int smp_num_cpus = 1; struct cpuinfo_x86 cpu_data[NR_CPUS]; error: Boot AP fail -> "SMP: Failed to boot CPU %d" log, fallback UP]
          | | [Global: extern int smp_num_cpus; [in smp.h; from smp_prepare_cpus (MADT scan for APs); Rationale: Limit to cpus= param; printk "Brought up %d CPUs" if success; 2.6.20: No x2apic (added 2.6.30)]
          | | [e.g., smp_init: if (max_cpus >1) find_smp_cpu_map(); boot_secondary_cpus(); smp_num_cpus=4; 2.6.20: boot_cpu_id=0; error: No MADT -> UP]
          |
          +--> Subsys Level Examples (Level 4 Subsys ~Line initcall4): subsys_initcall(pci_init); // drivers/pci/pci.c init PCI bus (pci_scan_bus, resource alloc)
          | |
          | +--> pci_init Fn (drivers/pci/pci.c Line ~2000-2100): pci_subsys_init(); pci_legacy_init(0); // pci_subsys_init: bus_register(&pci_bus_type); /sys/bus/pci; pci_legacy_init: pci_scan_bus(0, &pci_bus_root, &pci_bus_ops); enumerate devices
          | | [Rationale: Scan PCI bus 0 root (00:00.0 host bridge); 2.6.20: pci_scan_bus: pci_scan_child_bus(root) -> pci_scan_device (class=0x0101 IDE probe); global struct pci_bus *pci_bus_root; struct pci_driver ide_pci_driver; error: Scan fail (-ENOMEM) -> "pci_scan_bus failed" log, no devices]
          | | [Global: struct bus_type pci_bus_type = { .name = "pci", .match = pci_bus_match (vendor/device id), .uevent = pci_uevent (MODALIAS), .probe = NULL, .remove = NULL }; [in pci/bus.c; bus_register adds /sys/bus/pci; pci_scan_bus(parent, bus, ops) recursive scan bridges; 2.6.20: pci_bus_ops = &pci_root_ops (read_config_byte etc.); printk "PCI: Using configuration type 1 for base address 0x%08lx" for MMIO]
          | | [e.g., pci_init: printk "PCI: %s bus %d" type1; pci_scan_bus(0, &pci_bus_root, NULL) -> devices like 00:1f.1 IDE class 0x01018a; 2.6.20: No AER (PCI errors later)]
          |
          +--> FS Level Examples (Level 5 FS ~Line initcall5): fs_initcall(register_filesystem(&ext3_fs_type)); // fs/ext3/super.c register ext3 for mount
          | |
          | +--> register_filesystem Fn (fs/fs_context.c Line ~100, but 2.6.20 fs/super.c Line ~500): insert_filesystem(&ext3_fs_type); // Add to file_systems list for get_fs_type("ext3")
          | | [Rationale: Register fs for mount_block_root (fstype="ext3"); 2.6.20: ext3_fs_type = { .name = "ext3", .get_sb = ext3_get_sb, .kill_sb = kill_block_super }; global struct file_system_type *file_systems; error: List add fail (dup name) -> "ext3 already registered" log, mount fail]
          | | [Global: struct file_system_type ext3_fs_type = { .owner = THIS_MODULE, .name = "ext3", .get_sb = ext3_get_sb (sb_bread superblock read, ext3_fill_super), .kill_sb = kill_block_super, .fs_flags = FS_REQUIRES_DEV }; [in fs/ext3/super.c; get_sb: struct super_block *sb = get_sb_bdev(&ext3_fs_type, root_dev, NULL, flags, ext3_blksize, &ext3_blocksize); ext3_fill_super(sb, data, silent); printk "EXT3 FS on dev %s, internal journal"; 2.6.20: No ext4; journal init in ext3_load_journal]
          | | [e.g., register: list_add_tail(&ext3_fs_type.fs_list, &file_systems); mount "ext3" -> get_fs_type loop list match name; 2.6.20: Other fs_initcall vfat_init, ext2_init]
          |
          +--> 2.6.20 Specifics: Levels 3 arch ~20 calls (smp_init, pci_init, apm_init); 4 subsys ~50 (net_init, ide_init, scsi_init); 5 fs ~30 (ext3, vfat, proc); no deferred (EPROBE_DEFER=-110 not used, fail -ENXIO); __initcall3 arch_initcall(smp_init) __attribute__((section(".initcall3.arch"))); global int initcall_debug module_param (trace time if=1)
          | |
          | +--> [Overall: ~550-650ms (100 calls ~100ms); output: "SMP: Booting 1 CPUs" "PCI: Using INTx for MSI" "EXT3 FS on sda1, internal journal" if verbose; ERR: pci_init -ENOMEM -> no scan log continue (no devices, mount ok if built-in); Rationale: Hardware/fs ready (SMP CPUs, PCI devices probed, ext3 registered for root mount); next device/late phase 35]
    |
    v
Initcalls: Device/Late (Phase 35: do_initcalls end; ~650-750ms; [C]; device scsi_register, late calibrate_delay; 2.6.20: ~100 calls, late lpj final)










Initcalls: Device/Late (do_initcalls end)
[init/main.c; ~650-750ms; [C]; Line ~300; protected mode C continuation of do_initcalls in do_basic_setup; executes late levels 6-7 (device/late ~100 calls); modular dep-ordered init (device: scsi_register for blk drivers, ide_disk_init for disks; late: calibrate_delay for final lpj); globals: initcall_t *initcall_levels[7] (pointers to linker .initcallN sections), struct scsi_host_template scsi_template, unsigned long loops_per_jiffy (lpj); 2.6.20: Levels 6-7 ~100 calls (~1ms each); strict order (device after fs for mount, late after all for calibrate); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; post-arch/subsys/fs phase 34; next system_state=RUNNING phase 37; output: Printk "SCSI: <scsi_mod> %d.%d.%d" for scsi_register, "rtc: hctosys failed" for rtc_init, "Serial: 8250/16550 driver $Revision: 1.90 $ 4 ports"; error: Initcall -ENOMEM -> printk "initcall %p returned -12" continue (e.g., no scsi_register, no /dev/sd* mount fail if SCSI root); panic if SLAB_PANIC in rtc cache]
    |
    +--> Level Loop Continuation (Line ~310): for (int level=5; level < ARRAY_SIZE(initcall_levels) - 1; level++) { // Cont from fs level; levels 5 fs (ext3), 6 device (scsi/ide_disk), 7 late (calibrate)
          |
          +--> Call Loop: for (initcall_t *call = initcall_levels[level]; call < initcall_levels[level+1]; call++) { int err = do_one_initcall(*call); if (err < 0) printk(KERN_CRIT "%s: %p returned %d\n", __func__, *call, err); } // do_one_initcall calls fn(); log err
          | |
          | +--> do_one_initcall Detail (main.c Line ~850): if (!*call) printk(KERN_WARNING "do_one_initcall: No fn\n"); else { int err = (*call)(); if (err) printk(KERN_CRIT "initcall %p returned %d\n", *call, err); return err; } // Simple call, log err <0
          | | [Rationale: Run fn (e.g., scsi_register()); 2.6.20: KERN_CRIT=3 critical; no trace (CONFIG_FTRACE=n); global no; error: Fn crash -> #PF panic; -ENODEV common for no hw (log continue)]
          | | [Global: initcall_t __initcall6_start/end[] linker .initcall6.device (e.g., scsi_register); Rationale: Dep (device after fs for /dev/sd mount); ~100 calls levels 6-7 ~100ms]
          |
          +--> Device Level Examples (Level 6 Device ~Line initcall6): device_initcall(scsi_register); // drivers/scsi/scsi.c register SCSI bus for /dev/sd*
          | |
          | +--> scsi_register Fn (drivers/scsi/scsi.c Line ~100-200): scsi_init_sysctl(); scsi_host_init(); register_blkdev(SCSI_DISK_MAJOR, "sd"); // SCSI disk major 8 for /dev/sd[a-z]
          | | [Rationale: Register SCSI block dev (sd_probe for disks); 2.6.20: scsi_host_template { .proc_name = "scsi", .name = "scsi", .proc_info = scsi_proc_info }; global int SCSI_DISK_MAJOR=8; struct scsi_device_template scsi_template; error: Blkdev register fail (-EBUSY dup major) -> "scsi_register: blkdev register failed" log, no /dev/sd mount fail if SCSI root]
          | | [Global: struct bus_type scsi_bus_type = { .name = "scsi", .match = scsi_bus_match, .uevent = scsi_bus_uevent }; [in drivers/scsi/scsi_sysfs.c; bus_register adds /sys/bus/scsi; scsi_scan_host for devices; 2.6.20: No scsi-mq (legacy); printk "SCSI: <scsi_mod> %d.%d.%d</scsi_mod>" VERSION]
          | | [e.g., scsi_register: scsi_sysfs_init(); /sys/bus/scsi/devices; sd_probe (struct gendisk *gd = alloc_disk(16)) minors 0-255 for partitions; 2.6.20: SCSI host0 from ahci or libata probe]
          |
          +--> Late Level Examples (Level 7 Late ~Line initcall7): late_initcall(calibrate_delay); // kernel/time/tick.c final lpj calibrate after all init
          | |
          | +--> calibrate_delay Fn (kernel/time/tick.c Line ~100-150): if (!loops_per_jiffy) loops_per_jiffy = calibrate_delay_direct(); printk(KERN_INFO "Calibrating delay loop... %6lu.%02lu BogoMIPS (lpj=%lu)\n", loops_per_jiffy/(500000/HZ), (loops_per_jiffy/(5000/HZ)) % 100, loops_per_jiffy); // Direct loop calibrate 50ms PIT
          | | [Rationale: Final lpj after early (phase 24); BogoMIPS = lpj / (500000/HZ) MHz equiv; 2.6.20: calibrate_delay_direct asm loop 20000000 /5 =4e6 lpj ~2GHz; global unsigned long loops_per_jiffy; error: lpj=0 -> "Time: uncalibrated delay loop" fallback PIT, delays inaccurate]
          | | [Global: extern unsigned long loops_per_jiffy; [in include/linux/delay.h; used in udelay (lpj * us / 1000000 loops); Rationale: CPU speed for delay; 2.6.20: No calibrate_apic_clock (SMP later)]
          | | [e.g., lpj=1999800; BogoMIPS 1999.80 (lpj/(500000/250)=lpj/2000); printk always; 2.6.20: No tsc calibrate final (early enough)]
          |
          +--> Other Device Examples: device_initcall(rtc_init); // drivers/char/rtc.c init RTC (hctosys set time from CMOS)
          | |
          | +--> rtc_init Fn (drivers/char/rtc.c Line ~100-150): struct rtc_device *rtc = rtc_device_register("rtc0", &rtc_ops); if (rtc) hctosys(rtc); // hctosys: read CMOS time, set xtime.tv_sec from rtc_get_time
          | | [Rationale: Set wall time from hardware RTC; 2.6.20: rtc_ops {read_time = rtc_read_time (CMOS 0x00-0x09 BCD to sec)}; global struct rtc_class_ops rtc_ops; error: Register fail (-ENOMEM) -> "rtc_init: no mem" log, no /dev/rtc (time from boot 0)]
          | | [Global: struct class *rtc_class; [in drivers/char/rtc/class.c; class_register(&rtc_class); /sys/class/rtc/rtc0; 2.6.20: No hpet_rtc (added later)]
          | | [e.g., hctosys: struct rtc_time tm; rtc_ops.read_time(rtc, &tm); to_tm(xtime.tv_sec, &tm); xtime.tv_sec = mktime(tm.tm_year+1900, ...); printk "hctosys: %s" if success]
          |
          +--> Other Late Examples: late_initcall(calibrate_delay); device_initcall(serial8250_init); // drivers/serial/serial_core.c init UARTs (ttyS0-3)
          | |
          | +--> serial8250_init Fn (drivers/serial/8250.c Line ~100-200): serial8250_register_8250_port(4); // Probe 4 ports (0x3F8/IRQ4 ttyS0, 0x2F8/IRQ3 ttyS1, etc.); autoconfig_8250
          | | [Rationale: Init serial console (earlyprintk=ttyS0); 2.6.20: $Revision: 1.90 $ in printk; global struct uart_driver serial8250_reg; error: Port register fail -> "serial8250: too many ports" log, no ttyS0 (console fallback VGA)]
          | | [Global: struct uart_port serial8250_ports[4]; [in 8250.h; autoconfig: uart_add_one_port(&serial8250_reg, &serial8250_ports[i]); /dev/ttyS0; 2.6.20: No 8250_pci (pci probe separate)]
          | | [e.g., serial8250_init: printk "Serial: 8250/16550 driver $Revision: 1.90 $ 4 ports, IRQ sharing %sabled\n", share_irq ? "en" : "dis"; 2.6.20: share_irq=1 SA_SHIRQ]
          |
          +--> 2.6.20 Specifics: Levels 6 device ~70 calls (scsi_register, rtc_init, serial8250_init, usb_register, ide_disk_init); 7 late ~30 (calibrate_delay, console_map_init); no deferred (EPROBE_DEFER not, fail -ENODEV log); __initcall6 device_initcall(scsi_register) __attribute__((section(".initcall6.device"))); global int initcall_debug=0 (module_param trace time)
          | |
          | +--> [Overall: ~650-750ms (100 calls ~100ms); output: "SCSI: <scsi_mod> 2.6.20</scsi_mod>", "rtc: hctosys failed" if no CMOS, "Serial: 8250 driver 4 ports"; ERR: rtc_init -ENOMEM -> no /dev/rtc log continue; Rationale: Devices ready (SCSI /dev/sd, serial ttyS0, RTC time); late final calibrate; next system_state RUNNING phase 37]
    |
    v
system_state = SYSTEM_RUNNING (Phase 37: kernel_init; ~850ms; [C]; set RUNNING wake system_wq; 2.6.20: Signal halt paths; next init_post phase 38)













prepare_namespace() (init.c)
[init/do_mounts.c; ~750-850ms; [C]; Line ~800; protected mode C in kernel_init thread PID1 after do_basic_setup; prepares namespace by mounting root filesystem (pivot_root if initrd present to switch from tmp initrd to real root); mounts standard virtual fs (/proc for debug, /sys for sysfs devices, devpts for ttys, tmpfs for /dev/shm shared mem); globals: unsigned long root_mountflags, unsigned long initrd_start/end (from boot_params), char root_device_name[64], char root_fs_names[64], char saved_root_name[64]; 2.6.20: No devtmpfs (CONFIG_TMPFS=y but /dev ramfs static via mknod in initramfs); ramfs for early / (mount_root uses ramfs if no root=); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; post-phase 32 do_basic_setup (fs registered); next system_state=RUNNING phase 37; output: Printk "VFS: Mounted root (ext3 filesystem) on device 3:1.", "Mounted proc on /proc", "Mounted sysfs on /sys"; error: Mount fail (-ENODEV no dev) -> panic "VFS: Unable to mount root fs on unknown-block(%d,%d)"; pivot fail -> "No init found in initrd" panic]
    |
    +--> Root Name/Dev Parse (Line ~800): static noinline int prepare_namespace(void) { if (saved_root_name[0]) { strlcpy(root_device_name, saved_root_name, sizeof(root_device_name)); } else { strlcpy(root_device_name, root_dev_names[root_dev], sizeof(root_device_name)); } // From cmdline root=/dev/sda1 -> "sda1" or root_dev=0x0301 -> "hda1"
          |
          +--> Globals: char root_device_name[64] = "/dev/root"; char saved_root_name[64] = ""; unsigned int root_dev = 0; char *root_dev_names[] = {"", "hda", "hdb", ... "hdz", "hdaa", ...}; [Rationale: Name for mount_block_root; saved_root_name from parse_early_params "root="; root_dev from "root=0301" MKDEV(3,1); 2.6.20: No UUID parse (early strlcpy /dev/sda1); error: Bad name (long >64) -> truncate, mount fail ENOENT]
          | |
          | +--> Sub-Macro strlcpy (include/linux/string.h): size_t len = strlen(src); if (len >= size) len = size -1; memcpy(dest, src, len); dest[len] = 0; return len; [Rationale: Safe copy null-term; 2.6.20: No strncpy (unsafe); e.g., saved_root_name="sda1" -> root_device_name="/dev/sda1"]
          | | [e.g., if root=UUID=1234-5678 -> saved_root_name="UUID=1234-5678"; mount_block_root uses name for get_sb_bdev; 2.6.20: UUID parse in mount_root full]
          |
          +--> Initrd Pivot if Present (Line ~805): if (initrd_start) { /* Pivot from initrd tmp root to real */ root_mountflags |= MS_RDONLY; sys_mount(".", "/", NULL, MS_MOVE, NULL); // Move current / to /initrd sys_mount (fs/namespace.c Line ~2100): struct vfsmount *mnt = kern_mount(&rootfs_type); if (IS_ERR(mnt)) return PTR_ERR(mnt); sys_chroot("/initrd"); // Change root to /initrd (chroot("/initrd")) pivot_root(".", "/initrd/initrd"); // Pivot: move / to /initrd/initrd, new root /dev/root sys_chroot("."); // Back to new / } // Use real root
          | |
          | +--> Sub-Func pivot_root (fs/namespace.c Line ~2200-2300): if (new_root == old_root) return -EINVAL; // new_root="." old_root="/initrd/initrd" sys_umount("/initrd/initrd", MNT_DETACH); // Detach old root mount sys_mount(".", "/", NULL, MS_MOVE, NULL); // Move new / to old pos; [Rationale: Switch from initrd ramfs / to real ext3 /dev/sda1 (pivot moves mounts); 2.6.20: No pivot_root_fs (simple); global unsigned long initrd_start/end from boot_params.ramdisk_image/size; error: Umount fail (-EBUSY busy) -> "pivot_root: old_root busy" panic]
          | | [Global: extern unsigned long initrd_start, initrd_end; [in do_mounts.c; if initrd_end > initrd_start pivot; Rationale: Initrd for modular drivers (e.g., ide-pci.ko for root); 2.6.20: No sealed initrd (kernel checks size>0); sys_chroot: path_put(current->fs->root); current->fs->root = nd_get_submount(new_root); mntput(old_root)]
          | | [e.g., initrd_start=0x2000000: sys_mount(".", "/", NULL, MS_MOVE, NULL) moves initrd mounts; pivot_root(".", "/initrd/initrd") new_root=initrd ramfs, old_root=real /dev/sda1; success "Pivot root to /dev/sda1"; printk "Freeing initrd memory: %luk freed" if initrd_end]
          |
          +--> Root Filesystem Mount (Line ~820): mount_block_root(root_device_name, root_fstype); // Mount real root (e.g., /dev/sda1 ext3 ro)
          | |
          | +--> Sub-Func mount_block_root (do_mounts.c Line ~900-950): if (root_device_name[0] == 0) panic("VFS: Unable to mount root fs on unknown-block(0,0)"); struct nameidata nd; path_lookup(root_device_name, LOOKUP_FOLLOW, &nd); struct block_device *bdev = open_bdev_excl(root_device_name, 0, root_mountflags); if (IS_ERR(bdev)) panic("VFS: Cannot open root device %s (%ld)", root_device_name, PTR_ERR(bdev)); struct super_block *sb = get_sb_bdev(root_fstype, bdev, root_fstype_data, root_mountflags, ext3_blksize, &ext3_blocksize); if (IS_ERR(sb)) { blkdev_put(bdev, root_mountflags); panic("VFS: Unable to mount root fs on %s", root_device_name); } // get_sb_bdev: sget(fstype, NULL, set_bdev_super, bdev); sb->s_op = fstype->sops; sb->s_bdev = bdev; sb->s_dev = bdev->bd_dev; read_super(sb, data, silent); mnt = mount_bdev(fstype, flags, data, fill_super, sb);
          | | [Rationale: Mount block dev root (ext3 get_sb_bdev reads superblock via sb_bread(1)); root_fstype from cmdline rootfstype=ext3 or "ext2"; 2.6.20: No ramfs fallback (panic if no fs); global char root_fstype_data[64]=""; unsigned long root_mountflags (MS_RDONLY=1); error: open_bdev_excl -ENODEV no dev -> "VFS: Cannot open root device" panic; read_super fail (-EINVAL bad super) -> panic "Unable to mount root fs"]
          | | [Global: struct file_system_type *root_fstype = NULL; [set in parse_early_params "rootfstype=ext3" -> root_fstype = get_fs_type("ext3"); Rationale: FS type for get_sb; 2.6.20: ext3_fill_super(sb, data, silent) journal_init_inode; sb->s_magic = EXT3_SUPER_MAGIC=0xEF53; printk "VFS: Mounted root (ext3 filesystem) on device %u:%u.", MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev)]
          | | [e.g., root_device_name="/dev/sda1"; bdev = bdget(MKDEV(8,1)); open_bdev_excl: __invalidate_device(bdev, 1); sb = get_sb_bdev(&ext3_fs_type, bdev, NULL, MS_RDONLY, 1024, NULL); ext3_fill_super reads sb_bread(1, buffer) via ll_rw_block -> ide_do_rw_disk I/O; success "Mounted root (ext3) on device 8:1"; 2.6.20: No fstab parse (cmdline only)]
          |
          +--> Standard Mounts (Line ~830-850): mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL); mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL); mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "newinstance,ptmxmode=0000"); mount("tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "defaults"); // Virtual fs mounts
          | |
          | +--> Sub-Func mount (fs/namespace.c Line ~2000): struct nameidata nd; path_lookup(mntpt, LOOKUP_FOLLOW, &nd); struct vfsmount *mnt = kern_mount(&fstype); if (IS_ERR(mnt)) return PTR_ERR(mnt); sys_mount(mntpt, dir_fd, type, flags, data); // kern_mount: sb = fstype->get_sb(fstype, flags, data, mnt); mnt = mount_single(sb, data); attach_mnt(mnt, &nd.path);
          | | [Rationale: Mount virtual fs (/proc in-mem debug, /sys sysfs devices, /dev/pts pty slaves, /dev/shm tmpfs shared mem); MS_NOSUID no setuid, MS_NOEXEC no exec, MS_NODEV no dev nodes; 2.6.20: devpts "newinstance" separate pts, ptmxmode=0000 no auto ptmx; error: get_sb -ENOMEM -> "mount: %s failed" log continue (no /proc, ps fail but boot ok)]
          | | [Global: struct file_system_type proc_fs_type = { .name = "proc", .get_sb = proc_get_sb, .kill_sb = kill_litter_super, .fs_flags = FS_NOMOUNT }; [in fs/proc/root.c; proc_get_sb: sb = sget(fstype, NULL, proc_set_super, NULL); sb->s_op = &proc_sops; sb->s_root = proc_get_root(sb); sb->s_magic = PROC_SUPER_MAGIC=0x9FA0; Rationale: Virtual no block; similar for sysfs_fs_type, devpts_fs_type, tmpfs_fs_type; 2.6.20: No ramfs mount explicit (early / is ramfs)]
          | | [e.g., mount("proc", "/proc", "proc", 0x1000, NULL): kern_mount(&proc_fs_type) -> sb = proc_get_sb -> /proc/mounts etc.; success "Mounted proc on /proc"; devpts: get_sb_pseudo(&devpts_fs_type, "devpts", NULL, flags, 0); /dev/pts/0 for tty1; tmpfs: shmem_get_sb(sb, data) -> sb->s_op = &shmem_ops; /dev/shm semget; error: Pseudo sb fail -> no /dev/pts (no pty, telnet fail)]
          |
          +--> 2.6.20 Specifics: No devtmpfs (CONFIG_TMPFS=y but /dev mknod in initrd, no auto); ramfs for early / (mount_initrd if initrd); root_dev = old_decode_dev(root_dev) (from cmdline 0x0301 -> MKDEV(3,1)); no fstab (cmdline only); globals exported root_mountflags (module param sysctl fs.rootflags)
          | |
          | +--> [Overall: ~750-850ms (mounts ~100ms x4, pivot ~50ms if initrd); output: "VFS: Mounted root (ext3) on device 3:1", "Mounted proc/sysfs/devpts/tmpfs"; ERR: Root mount -EINVAL bad super -> panic "Unable to mount root fs"; Rationale: User-ready namespace (/proc for ps, /sys for hw, /dev for ttys/shm); next system_state RUNNING phase 37]
    |
    v
system_state = SYSTEM_RUNNING (Phase 37: kernel_init ~850ms; [C]; set RUNNING=1, wake_up system_wq for halt/restart; 2.6.20: Signal waiting procs; next init_post phase 38)









system_state = SYSTEM_RUNNING
[init/main.c; ~850ms; [C]; Line ~570; protected mode C in kernel_init thread PID1 after prepare_namespace; sets global system_state from SYSTEM_BOOTING to SYSTEM_RUNNING; wakes system_wq wait queue for any waiting processes (e.g., halt/restart paths); signals kernel ready for full operations (user helpers, shutdown); globals: enum system_states system_state (global int), wait_queue_head_t system_wq; 2.6.20: Simple enum (BOOTING=0, RUNNING=1, HALT=2, POWER_OFF=3, RESTART=4); no advanced states (suspend later); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1; post-phase 36 prepare_namespace (root mounted); next init_post phase 38; output: None (silent set); error: Wrong set (e.g., to HALT early) -> helpers fail (modprobe stuck, reboot broken); rationale: Gate post-boot ops (e.g., usermodehelper check RUNNING before exec)]
    |
    +--> System State Set (Line ~570): system_state = SYSTEM_RUNNING; // Change from SYSTEM_BOOTING (0) to SYSTEM_RUNNING (1); enum in include/linux/kernel.h
          |
          +--> Enum Definition: enum system_states { SYSTEM_BOOTING, SYSTEM_RUNNING, SYSTEM_HALT, SYSTEM_POWER_OFF, SYSTEM_RESTART }; // Simple 5 states; global extern enum system_states system_state; [Rationale: State machine for kernel phases; BOOTING gates initcalls (e.g., do_initcalls skip if not BOOTING); RUNNING enables user exec, halt/restart; 2.6.20: No suspend states (ACPI_S0 later); used in kernel_halt (if RUNNING kernel_power_off); error: Set to HALT early -> panic "Kernel not running" in reboot]
          | |
          | +--> Global Usage: in usermodehelper.c if (system_state != SYSTEM_RUNNING) return -EPERM; // No exec if not running; in kernel/reboot.c kernel_restart(reason) if (system_state == SYSTEM_RUNNING) machine_restart(reason); [Rationale: Prevent ops during boot (e.g., modprobe during initcalls); 2.6.20: No lock (simple int); printk "Kernel state to RUNNING" none]
          | | [e.g., system_state=1; 2.6.20: No sysctl export (module_param? No); error: Volatile missing (multi-CPU race? Rare early single thread)]
          |
          +--> Wake System Wait Queue (Line ~575): wake_up(&system_wq); // Wake all sleepers on system_wq (e.g., waiting for RUNNING in halt paths);
          | |
          | +--> Sub-Func wake_up (kernel/wait.c Line ~200-250): void __wake_up(wait_queue_head_t *q, unsigned int mode, int nr, void *key) { unsigned long flags; spin_lock_irqsave(&q->lock, flags); __wake_up_common(q, mode, nr, 0, key); spin_unlock_irqrestore(&q->lock, flags); } // Loop task_list, if wait condition met, TASK_RUNNING, dequeue
          | | [Rationale: Signal ready for shutdown/restart (e.g., kernel_halt sleeps on system_wq until RUNNING); 2.6.20: wake_up_all(&system_wq) variant; global DECLARE_WAIT_QUEUE_HEAD(system_wq); [in reboot.c; used in kernel_halt: wait_event(system_wq, system_state == SYSTEM_RUNNING); kernel_power_off(); error: Lock irq save fail (nested) -> spinlock deadlock rare]
          | | [Global: wait_queue_head_t system_wq = { .lock = __SPIN_LOCK_UNLOCKED(system_wq.lock), .task_list = LIST_HEAD_INIT(system_wq.task_list) }; [Rationale: Wait for boot complete before power off; 2.6.20: No per-CPU wq; empty early (no sleepers)]
          | | [e.g., wake_up_all: nr_exclusive=0 wake all; if sleeper current->state == TASK_UNINTERRUPTIBLE && wait->func(key) true, TASK_RUNNING, list_del; 2.6.20: No wait_event_timeout (simple)]
          |
          +--> 2.6.20 Specifics: system_state simple int (no lock); system_wq unused early (no halt during boot); enum in kernel.h exported; no sysctl kernel.state (added later); used in kernel_panic "Kernel panic - not syncing: Attempted to kill init!" if !RUNNING
          | |
          | +--> [Overall: ~850ms (set/wake ~1ms); silent; ERR: Wake on empty q -> nop; Rationale: Kernel steady signal (user ready, shutdown safe); next init_post free/exec phase 38]
    |
    v
init_post() (Phase 38: main.c; ~850-860ms; [C]; free_initmem reclaim __init sections; mark_rodata_ro protect; proc_sys_init mount /proc/sys; run_init_process exec /sbin/init; 2.6.20: Slab free ~2-4MB, set_memory_ro PTE RO; state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1 RUNNING)












init_post() (main.c)
[init/main.c; ~850-860ms; [C]; Line ~900; protected mode C in kernel_init thread PID1 end after system_state=RUNNING; finalizes boot by freeing __init memory sections (~2-4MB reclaim for slab), marking rodata read-only (PTE protect), mounting /proc/sys for tunables, executing /sbin/init as PID1 user-space handover (do_execve ELF load); globals: unsigned long __init_begin/end (linker sections ~2MB text/data/bss), unsigned long __rodata_start/end (~1MB const data), struct super_block *proc_sys_sb, char *init_fallbacks[4] = {"/sbin/init", "/etc/init", "/bin/init", "/bin/sh"}; 2.6.20: free_initmem loops mem_map Slab pages in __init, slab_free; mark_rodata_ro temp WP=0 set PTE RO WP=1; proc_sys_init mount proc /proc/sys; run_init_process chain fallback panic "No init found"; state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1 RUNNING; post-phase 37 state set; next free_initmem detail phase 39; output: Printk "Freeing unused kernel memory: %dk freed", "Write protecting the kernel rodata: %luk"; error: Exec fail all fallback -> panic "Kernel panic - not syncing: No working init found. Try passing init= option"; free fail -> leak mem (no reclaim, OOM later)]
    |
    +--> Initmem Free (Line ~900): free_initmem(); // Walk __init_begin to __init_end, free Slab pages (text/data/bss __init sections ~2-4MB) to buddy allocator
          |
          +--> Sub-Func free_initmem (mm/init.c Line ~100-150): unsigned long pfn_start = __pa(__init_begin) >> PAGE_SHIFT; unsigned long pfn_end = __pa(__init_end) >> PAGE_SHIFT; for (unsigned long pfn = pfn_start; pfn < pfn_end; pfn++) { if (!pfn_valid(pfn)) continue; struct page *page = mem_map + pfn; if (PageReserved(page) || !PageSlab(page)) continue; if (slab_unmergeable(page)) continue; struct kmem_cache *s = virt_to_cache(page_address(page)); if (s && s->flags & SLAB_SLAB_FREE) { /* Free slab */ kmem_cache_free(s, page_address(page)); } else { /* Free page */ __free_pages(page, 0); } } printk(KERN_INFO "Freeing unused kernel memory: %ldk freed\n", (pfn_end - pfn_start) * PAGE_SIZE / 1024); // Calc reclaimed
          | |
          | +--> Globals: extern unsigned long __init_begin, __init_end; [linker symbols in vmlinux.lds.S .init.text .init.data .init.rodata; Rationale: Sections marked __init for reclaim; 2.6.20: ~2MB text (init fns), 1MB data/bss; PageSlab = PageSlab(page) if page->flags & (1<<PG_slab); virt_to_cache(addr) = slab_cache(addr); error: Bad pfn (outside mem_map) -> skip; slab_free fail (ref leak) -> leak page (no OOM early)]
          | | [Rationale: Reclaim boot mem after use (e.g., free do_basic_setup code); 2.6.20: __free_pages to buddy (free_list[0]); printk always; e.g., "Freeing unused kernel memory: 232k freed"; used post-all initcalls]
          | | [e.g., pfn_start ~0x1000 (init text phys), pfn_end ~0x2000; for each Slab page in range, kmem_cache_free(s, obj) frees objects; if no Slab, __free_pages(page, 0) to free_area[0]; total reclaimed ~2MB]
          |
          +--> Rodata Mark Read-Only (Line ~905): mark_rodata_ro(); // Temp disable WP, set PTEs for __rodata_start to __rodata_end RO (noexec? No), re-enable WP
          | |
          | +--> Sub-Func mark_rodata_ro (mm/init.c Line ~200-250): unsigned long flags = local_save_cr0(); // Save CR0; cr0 = read_cr0(); cr0 &= ~0x10000; write_cr0(cr0); // Temp WP=0 (allow write kernel); set_memory_ro((unsigned long)__rodata_start, (__rodata_end - __rodata_start) >> PAGE_SHIFT); write_cr0(flags); // Restore WP=1
          | | [Rationale: Protect const rodata (strings/tables) from overwrite (security); set_memory_ro calls change_page_attr (pte_wrprotect pte); 2.6.20: __rodata_start/end linker .rodata (~1MB strings); global unsigned long __rodata_start/end; error: Bad range (overlap code) -> rodata RW leak (exploit); printk "Write protecting the kernel rodata: %luk" ((end-start)>>10)]
          | | [Global: #define read_cr0() ({ unsigned long cr0; asm("mov %%cr0,%0" : "=r" (cr0) :); cr0; }); #define write_cr0(cr0) asm("mov %0,%%cr0" : : "r" (cr0)); [in asm-i386/system.h; local_save_cr0 = read_cr0; Rationale: Atomic CR0 WP toggle; 2.6.20: No set_memory_nx (NX later)]
          | | [e.g., rodata 0xC0200000-0xC0210000; num_pages=256; change_page_attr loop pte = ptep_get_and_clear(pte), pte_wrprotect(pte), ptep_set_wrprotect; success "rodata: 1024k protected"; 2.6.20: No __init rodata separate]
          |
          +--> Proc Sys Mount (Line ~910): proc_sys_init(); // Mount /proc/sys subtree for tunables (/proc/sys/kernel/threads-max etc.)
          | |
          | +--> Sub-Func proc_sys_init (fs/proc/proc_sysctl.c Line ~50-100): struct proc_dir_entry *pde = proc_mkdir("sys", proc_root); if (pde) { register_sysctl_table(root_table); } // /proc/sys = dir; root_table = sysctl table for kernel/vm/fs/net
          | | [Rationale: Expose sysctls (/proc/sys/kernel/panic =1); 2.6.20: proc_mkdir("sys", NULL); register_sysctl_table (kern_table, vm_table etc.); global struct ctl_table_header *root_table; error: Mkdir fail (-ENOMEM) -> "proc_sys_init: mkdir failed" log, no /proc/sys (sysctl fail but boot ok)]
          | | [Global: extern struct ctl_table kern_table[], vm_table[], fs_table[], net_table[]; [in kernel/sysctl.c etc.; struct ctl_table { const char *procname; int ctl_name; void *data; int maxlen; mode_t mode; ctl_table *child; proc_handler *proc_handler; ... }; Rationale: Sysctl tree /proc/sys/kernel/hostname = sysctl_string; 2.6.20: No selinux sysctls; register_sysctl_table walks tree, proc_create for each]
          | | [e.g., proc_mkdir("sys", proc_root) -> pde->name="sys"; register_sysctl_table(kern_table) -> proc_create("kernel", 0555, sys_pde, &kern_fops); kern_table[0] = {"kernel", CTL_KERN, sysctl_table, 0, 0555, kern_table_child}; /proc/sys/kernel/threads-max = sysctl_intvec; printk none]
          |
          +--> Init Process Exec (Line ~915): run_init_process("/sbin/init"); // Exec first user program PID1; fallback chain if fail
          | |
          | +--> Sub-Func run_init_process (main.c Line ~950-960): static char *init_fallbacks[] = { "/sbin/init", "/etc/init", "/bin/init", "/bin/sh" }; for (int i=0; i < ARRAY_SIZE(init_fallbacks); i++) { if (!do_execve(init_fallbacks[i], (char *const *)init_argv, (char *const *)init_envp)) return; } panic("No init found. Try passing init= option"); // do_execve loads ELF binary, replaces PID1 image
          | | [Rationale: Handover to user-space init (SysV/upstart reads /etc/inittab); init_argv = {"/sbin/init", NULL}; init_envp = current->envp (HOME=/ TERM=linux); 2.6.20: Fallback to sh if no init (emergency shell); global char *init_argv[2], *init_envp[INIT_ENV_ARG_LIMIT=32]; error: All exec fail (-ENOENT) -> panic "Kernel panic - not syncing: No working init found. Try passing init= option"]
          | | [Global: extern char **init_envp, *init_argv[]; [in main.c; envp from init_task (PATH=/usr/bin:/bin TERM=linux etc.); do_execve (fs/exec.c Line ~1000): search_binary_handler (binfmt_elf for /sbin/init ELF); flush_old_exec; load_elf_binary (map text/data/stack, set_regs eip=entry esp=stack); success 0, fail -ENOEXEC]
          | | [e.g., do_execve("/sbin/init", argv, envp) -> open_exec -> load_elf_image -> map VMA text 0x8048000; set_regs(eip=0x80482E0 entry, esp=0xBFFFFE00 argc/argv/envp); PID1 mm = new mm; reparent orphans to PID1; printk none; 2.6.20: SysV init /etc/inittab default runlevel 3 multiuser]
          |
          +--> 2.6.20 Specifics: No cgroup v1 full (basic cpusets in do_basic_setup); uevent_helper exec /sbin/hotplug for udev events; noop_bdi for tmpfs no dirty track; initcalls strict levels (no deferred probe, -EPROBE_DEFER not, fail -ENXIO log); __initcall6 device_initcall(scsi_register) section(".initcall6.device"); global int initcall_debug=0 module_param (trace time if=1)
          | |
          | +--> [Overall: ~420-430ms (do_basic_setup ~300ms, mount ~50ms, exec ~20ms); output: "VFS: Mounted root (ext3) on device 3:1" / "Freeing unused kernel memory: 232k freed"; ERR: Mount -EINVAL bad super -> panic "Unable to mount root fs"; Rationale: Final setup before user handover (drivers probed, root mounted, /proc/sys ready); next init_post exec phase 38]
    |
    v
Initcalls: Core/Postcore (Phase 33: do_initcalls in do_basic_setup; ~500-550ms; [C]; core/postcore levels page_launder_init kmem_cache_init rcu_init_node; 2.6.20: ~50 calls)














Free Initmem & Mark RO (init_post)
[init/main.c; ~860-870ms; [C]; Line ~950; protected mode C in init_post (called from kernel_init end after system_state=RUNNING); reclaims __init memory sections (~2-4MB text/data/bss) by freeing Slab pages to buddy allocator; marks rodata read-only via PTE wrprotect (temp WP=0, set RO, WP=1); globals: unsigned long __init_begin/end (linker sections ~2MB), unsigned long __rodata_start/end (~1MB const), struct page *mem_map (lowmem pages); 2.6.20: free_initmem loops pfn __init_range if PageSlab, slab_free objects; mark_rodata_ro change_page_attr pte_wrprotect; state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1 RUNNING; post-phase 37 state set; next exec init phase 40; output: Printk "Freeing unused kernel memory: %dk freed", "Write protecting the kernel rodata: %luk"; error: Slab free fail (ref leak) -> leak mem (OOM later); rodata protect fail (WP stuck=0) -> kernel RW exploit risk]
    |
    +--> Initmem Reclaim (Line ~900): free_initmem(); // Walk __init_begin to __init_end phys range, free Slab-allocated pages/objects in buddy; ~2-4MB reclaimed post-all initcalls
          |
          +--> Sub-Func free_initmem (mm/init.c Line ~100-150): unsigned long init_begin_pfn = __pa(__init_begin) >> PAGE_SHIFT; unsigned long init_end_pfn = __pa(__init_end) >> PAGE_SHIFT; for (unsigned long pfn = init_begin_pfn; pfn < init_end_pfn; pfn++) { if (!pfn_valid_within(pfn)) continue; struct page *page = mem_map + pfn; if (PageReserved(page) || PageSlab(page) == 0) continue; struct kmem_cache *s = virt_to_cache(page_address(page)); if (s && (s->flags & SLAB_SLAB_FREE)) { kmem_cache_free(s, page_address(page)); } else { __free_pages_bootmem(page, 0); } } printk(KERN_INFO "Freeing unused kernel memory: %ldk freed\n", (init_end_pfn - init_begin_pfn) * PAGE_SIZE / 1024); // Total reclaimed
          | |
          | +--> Globals: extern unsigned long __init_begin, __init_end; [linker vmlinux.lds.S .init.text { *(.init.text) } .init.data { *(.init.data) } .init.rodata { *(.init.rodata) }; Rationale: Mark boot fns __init __attribute__((section(".init.text"))); ~2MB text (do_basic_setup etc.), 1MB data/bss; 2.6.20: __pa(x) = (unsigned long)(x) - PAGE_OFFSET (virt to phys); pfn_valid_within = pfn <= max_low_pfn; error: Bad __init_range (linker overlap) -> free wrong pages (leak or corrupt); PageSlab = (page->flags & (1UL << PG_slab))]
          | | [Rationale: Reclaim boot mem after use (e.g., free initcalls code); 2.6.20: __free_pages_bootmem to bootmem pool (free_bootmem PFN_PHYS(pfn), PAGE_SIZE); kmem_cache_free frees slab objects if full slab free; used after all __init fns run; printk always end]
          | | [Global: struct page *mem_map; [lowmem pfn array; PageReserved = flags & (1<<PG_reserved) (BIOS/ACPI resvd); virt_to_cache(addr) = ((struct slab *)(addr - addr % PAGE_SIZE))->cache; Rationale: Slab pages in __init (e.g., dentry_cache alloc during init); error: Free non-slab -> __free_pages assert fail panic]
          | | [e.g., init_begin_pfn ~0x1000 (init text phys 4MB), init_end_pfn ~0x2000 (6MB); for pfn=0x1200 PageSlab(dentry slab) s=virt_to_cache(0x480000) = dentry_cachep; kmem_cache_free(dentry_cachep, obj); total freed 2048k; 2.6.20: No __per_cpu_start/end separate (percpu in .data)]
          |
          +--> Rodata Protection (Line ~905): mark_rodata_ro(); // Temp disable CR0.WP=0, set PTE WRPROT for __rodata_start to __rodata_end, re-enable WP=1; protect const data
          | |
          | +--> Sub-Func mark_rodata_ro (mm/init.c Line ~200-250): unsigned long cr0 = read_cr0(); unsigned long flags = cr0; cr0 &= ~CR0_WP; write_cr0(cr0); // Temp WP=0 allow write PTE set_memory_ro((unsigned long)__rodata_start, (__rodata_end - __rodata_start) >> PAGE_SHIFT); cr0 = read_cr0(); cr0 |= CR0_WP; write_cr0(cr0); // Restore WP=1
          | | [Rationale: Protect rodata strings/tables from overwrite (security); set_memory_ro calls change_page_attr (for each page: ptep = pte_offset_map(pmd, addr), pte = *ptep, pte = pte_wrprotect(pte), set_pte(ptep, pte)); 2.6.20: __rodata_start/end linker .rodata (~1MB const char *str, tables); global unsigned long __rodata_start/end; error: WP stuck=0 (bad cr0) -> kernel RW (exploit); temp WP=0 risks write during set]
          | | [Global: #define read_cr0() asm("mov %%cr0,%0":"=r" (cr0)::"memory"); #define write_cr0(cr0) asm("mov %0,%%cr0"::"r" (cr0):"memory"); [in asm-i386/system.h; CR0_WP=0x10000 bit16; Rationale: Atomic toggle WP for PTE change (supv write kernel needs WP=0); 2.6.20: No set_memory_nx (NX CR4 bit20 later)]
          | | [e.g., rodata 0xC0200000-0xC0210000 num_pages=256/4=64; change_page_attr loop pmd = pmd_offset(pgd, addr), ptep = pte_offset_map(pmd, addr), pte_wrprotect(pte_mkold(pte)), set_pte(ptep, pte); printk "Write protecting the kernel rodata: %luk\n", (end-start)>>10; success 1024k]
          |
          +--> 2.6.20 Specifics: __init sections .init.text/data/rodata separate reclaim; free_initmem only Slab (__free_pages_bootmem if !Slab); mark_rodata_ro only rodata (no text, already RX); no __per_cpu reclaim (percpu in .data); globals exported __init_begin/end (vmlinux.sym)
          | |
          | +--> [Overall: ~860-870ms (free loop ~50ms, rodata set ~20ms); output: "Freeing unused kernel memory: 232k freed" "Write protecting the kernel rodata: 1024k"; ERR: Free non-init -> assert fail panic; Rationale: Reclaim/reprotect post-boot (security/efficiency); next exec /sbin/init phase 40]
    |
    v
do_execve("/sbin/init") (Phase 40: exec.c; ~870-900ms; [C]; search binfmt_elf for /sbin/init ELF; 2.6.20: SysV init; state: RUNNING handover to user)









do_execve("/sbin/init") (exec.c)
[fs/exec.c; ~870-900ms; [C]; Line ~1000; protected mode C in run_init_process (init_post calls); executes first user-space program /sbin/init as PID1 (replaces kernel_init thread image with ELF binary); searches binfmt_elf for ELF loader; flushes old exec (mm/files/sig); loads ELF (map text/data/stack, set regs eip/esp); globals: struct linux_binprm bprm (binary params), struct mm_struct *current->mm (new mm for PID1), char *init_argv[2] = {"/sbin/init", NULL}, char **init_envp (current->envp ~32 env vars); 2.6.20: SysV init ELF (Upstart optional); fallback /etc/init /bin/init /bin/sh -> panic "No init found"; state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1 RUNNING; post-phase 38 init_post; next ELF load phase 41; output: None (silent exec, login prompt from init); error: Exec -ENOENT no file -> fallback chain; all fail -> panic "Kernel panic - not syncing: No working init found. Try passing init= option"]
    |
    +--> Binary Param Prep (Line ~1000): struct linux_binprm *bprm = kmem_cache_alloc(bprm_cachep, GFP_KERNEL); if (!bprm) return -ENOMEM; memset(bprm, 0, sizeof(*bprm)); bprm->filename = "/sbin/init"; bprm->argc = 1; bprm->envc = 0; bprm->mm = NULL; bprm->p = PAGE_SIZE * MAX_ARG_PAGES; // Prep bprm for load_binary_file
          |
          +--> Sub-Func bprm_cache_init (exec.c Line ~50): kmem_cache_create("bprm_cache", sizeof(struct linux_binprm), 0, SLAB_NO_DEBUG, NULL); // Cache for bprm (~512B: filename[128], argv/envp ptrs, mm, p (stack top)); [Rationale: Fast alloc for execve; 2.6.20: SLAB_NO_DEBUG no redzone; global struct kmem_cache *bprm_cachep; error: Cache fail -> kmalloc slow (exec ok but slower)]
          | |
          | +--> Global: struct linux_binprm { char filename[128]; char *interp; unsigned interp_flags; unsigned interp_data; unsigned long loader, exec; struct page *page[16]; struct mm_struct *mm; unsigned long p; int argc, envc; unsigned long min_coredump; unsigned long arg_start, arg_end, env_start, env_end; unsigned long stack_top; int sh_bang; }; [Rationale: Params for binfmt load (filename, argv=init_argv, envp=init_envp, interp="/lib/ld.so" if dynamic); 2.6.20: MAX_ARG_PAGES=32 (128KB stack); error: kmem_cache_alloc -ENOMEM -> "bprm_cache_alloc failed" return -ENOMEM (exec fail rare)]
          | | [e.g., bprm_cachep->object_size=512B; bprm->filename="/sbin/init\0"; bprm->argc=1 (argv[0]="/sbin/init"); bprm->envc=count envp (~32 HOME=/ TERM=linux); used in load_elf_binary]
          |
          +--> Binary Search Start (Line ~1010): int retval = search_binary_handler(bprm); if (retval < 0) { /* Fail */ put_bprm(bprm); return retval; } // Loop registered binfmt (binfmt_elf first for ELF)
          | |
          | +--> Sub-Func search_binary_handler (exec.c Line ~1100-1150): struct linux_binfmt *fmt; for (fmt = formats; fmt; fmt = fmt->next) { retval = fmt->load_binary(bprm); if (retval == 0) { /* Success */ break; } if (retval != -ENOEXEC) return retval; } // Try each format (ELF, script, flat, aout)
          | | [Rationale: Multi-format exec (ELF primary, #! script, binfmt_misc); 2.6.20: formats list head binfmt_list, binfmt_elf registered early; global LIST_HEAD(binfmt_list); error: All -ENOEXEC -> "No handler for /sbin/init" fallback]
          | | [Global: struct linux_binfmt { struct list_head lh; struct module *module; int (*load_binary)(struct linux_binprm *); int (*load_shlib)(struct file *); int (*core_dump)(long signr, struct pt_regs *regs, struct file *file, unsigned long limit); void (*free)(struct linux_binprm *); int (*map)(struct linux_binprm *); }; [Rationale: load_binary loads ELF; binfmt_elf = {load_binary = load_elf_binary, core_dump = elf_core_dump}; register_binfmt(&binfmt_elf); 2.6.20: No binfmt_java; load_shlib for dynamic loader]
          | | [e.g., fmt = &binfmt_elf; retval = load_elf_binary(bprm) (0 success); formats = &binfmt_elf -> &binfmt_script -> NULL; 2.6.20: No binfmt_misc early (module later)]
          |
          +--> 2.6.20 Specifics: bprm_cachep from SLAB (phase 26 inode? No, separate); formats list protected by binfmt_lock spinlock; init_argv/envp from main.c (argv[0]="/sbin/init"); no seccomp (added 2.6.12 but basic); globals exported current->mm (new for PID1)
          | |
          | +--> [Overall: ~870-900ms (bprm alloc ~1ms, search ~5ms); silent; ERR: No binfmt (unreg) -> -EINVAL "No binary handler"; Rationale: Handover PID1 kernel to user init; next ELF load phase 41]
    |
    v
ELF Load & User Space Entry (Phase 41: binfmt_elf.c load_elf_binary; ~900-950ms; [C]; open_exec map VMA text/data/stack; set_regs eip/esp; 2.6.20: Map text 0x8048000; state: handover PID1 user CS=3)








ELF Load & User Space Entry (execve)
[fs/binfmt_elf.c load_elf_binary; ~900-950ms; [C]; Line ~200; protected mode C in binfmt_elf->load_binary (from search_binary_handler in do_execve); loads /sbin/init ELF binary as PID1 user-space image (replaces kernel_init thread); opens file, maps VMA text/data/heap/stack, sets regs eip/esp for entry; globals: struct linux_binfrm bprm (filename="/sbin/init"), struct mm_struct *current->mm (new mm for PID1), struct elf_phdr *elf_phdata (program headers), unsigned long elf_map (VMA base); 2.6.20: Text map @0x8048000 (1.5MB default), data @0x804A000, stack @0xBFFFF000 (8MB ELF_ET_DYN_BASE=0x400000 dynamic? No, ET_EXEC fixed); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1 RUNNING; post-phase 40 do_execve bprm prep; next context switch phase 42; output: None (silent load, init outputs login); error: Load -ENOEXEC bad ELF -> fallback /etc/init; map -ENOMEM -> "Out of memory" -ENOMEM exec fail fallback]
    |
    +--> File Open & Validate (Line ~200): struct file *file = open_exec(bprm->filename); if (IS_ERR(file)) return PTR_ERR(file); // open_exec: path_openat(AT_FDCWD, filename, O_RDONLY|O_LARGEFILE|FMODE_EXEC, LOOKUP_FOLLOW)
          |
          +--> Sub-Func open_exec (exec.c Line ~1100): struct nameidata nd; retval = path_lookup(filename, LOOKUP_FOLLOW, &nd); if (retval) return ERR_PTR(retval); struct file *file = dentry_open(&nd.path, O_RDONLY | O_LARGEFILE | __FMODE_EXEC, current_cred()); path_put(&nd.path); if (IS_ERR(file)) return file; // dentry_open: f_op = file->f_dentry->d_inode->i_fop; f_count++; return file;
          | |
          | +--> Globals: struct file *file; [in binfmt_elf; struct file { struct path f_path; struct inode *f_inode; const struct file_operations *f_op; spinlock_t f_lock; atomic_t f_count; unsigned int f_flags; ... }; Rationale: Open /sbin/init RDONLY largefile exec; 2.6.20: LOOKUP_FOLLOW=0x00000001 (follow symlinks); __FMODE_EXEC=0x00000020 (mark exec); error: path_lookup -ENOENT no file -> -ENOENT fallback; dentry_open -ENOMEM no f_struct -> -ENOMEM]
          | | [Rationale: Validate exec perm (f_inode->i_mode & 0111, may_exec); 2.6.20: No seccomp filter early; f_path.dentry = dget(dentry); f_path.mnt = mntget(mnt); f_flags = O_RDONLY | O_LARGEFILE; printk none]
          | | [e.g., filename="/sbin/init"; path_lookup -> nd.dentry = d_lookup (hash "/sbin/init"); file = dentry_open -> f_op = elf_fops (read=elf_read); f_count=1; success file->f_inode->i_size ~50KB]
          |
          +--> ELF Header Parse (Line ~210): retval = elf_read_header(file, &ehdr, bprm->filename); if (retval < 0) { allow_write_access(file); fput(file); return retval; } // elf_read_header: kernel_read(file, 0, &ehdr, sizeof(ehdr)); if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ... ) return -ENOEXEC;
          | |
          | +--> Sub-Func elf_read_header (binfmt_elf.c Line ~300-320): Elf64_Ehdr *ehdr = (Elf64_Ehdr *)bprm->buf; retval = read_code(file, 0, sizeof(*ehdr), bprm->buf); if (retval != sizeof(*ehdr)) return -EIO; if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) || ehdr->e_ident[EI_CLASS] != ELFCLASS32 || ehdr->e_ident[EI_DATA] != ELFDATA2LSB || ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_386) return -ENOEXEC; // Validate ELF magic 0x7F 'ELF', 32b little-endian, exec type, i386 machine
          | | [Rationale: Parse e_ident[16B] magic/version/class/data; 2.6.20: ELFCLASS32=1, EM_386=3; read_code = kernel_read (file->f_pos=0, count=52B header); global const char ELFMAG[4] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3}; = "\177ELF"; error: read -EIO short -> -EIO; bad magic -> -ENOEXEC fallback binfmt_script]
          | | [Global: struct elfhdr { unsigned char e_ident[EI_NIDENT]; Elf32_Half e_type; Elf32_Half e_machine; Elf32_Word e_version; Elf32_Addr e_entry; Elf32_Off e_phoff; Elf32_Off e_shoff; Elf32_Word e_flags; Elf32_Half e_ehsize; Elf32_Half e_phentsize; Elf32_Half e_phnum; Elf32_Half e_shentsize; Elf32_Half e_shnum; Elf32_Half e_shstrndx; }; [Rationale: e_type=ET_EXEC=2 fixed load, ET_DYN=3 PIC; e_entry = 0x80482E0; e_phoff=0x34 (program headers); EI_NIDENT=16; 2.6.20: No ET_REL (relocatable)]
          | | [e.g., kernel_read: do { char *buf = bprm->buf + copied; retval = file->f_op->read(file, buf, count-copied, &file->f_pos); copied += retval; } while (copied < count && retval >0); ehdr.e_ident[0]=0x7F, [1]='E', [2]='L', [3]='F'; success ehdr.e_phnum=5 (text/data/dynamic/symtab/strtab)]
          |
          +--> Program Headers Load (Line ~220): retval = load_elf_phdrs(file, &elf_ex, &ehdr, bprm); if (retval < 0) return retval; // load_elf_phdrs: lseek(file, ehdr.e_phoff, SEEK_SET); for (int i=0; i<ehdr.e_phnum; i++) { retval = kernel_read(file, ehdr.e_phoff + i*ehdr.e_phentsize, phdr, sizeof(phdr)); if (retval != sizeof(phdr)) return -EIO; if (phdr->p_type != PT_LOAD) continue; phdrs[num].p_vaddr = phdr->p_vaddr; ... num++; }
          | |
          | +--> Sub-Func load_elf_phdrs (binfmt_elf.c Line ~350-400): struct elf_phdr *phdr = elf_phdata = (struct elf_phdr *)kmalloc(ehdr.e_phnum * sizeof(struct elf_phdr), GFP_KERNEL); if (!elf_phdata) return -ENOMEM; file->f_pos = ehdr.e_phoff; for (int i=0; i<ehdr.e_phnum; i++) { retval = kernel_read(file, file->f_pos, &phdr[i], ehdr.e_phentsize); file->f_pos += ehdr.e_phentsize; if (retval != ehdr.e_phentsize) { kfree(elf_phdata); return -EIO; } } // Copy phdrs to kmalloc; count PT_LOAD (loadable segments)
          | | [Rationale: Parse e_phnum program headers (PT_LOAD text/data, PT_DYNAMIC interp, PT_INTERP "/lib/ld-linux.so.2"); 2.6.20: e_phentsize=32B, e_phnum~5-10; global struct elf_phdr *elf_phdata; error: kmalloc -ENOMEM -> -ENOMEM exec fail; read short -> -EIO]
          | | [Global: struct elf_phdr { Elf32_Word p_type; Elf32_Off p_offset; Elf32_Addr p_vaddr; Elf32_Addr p_paddr; Elf32_Word p_filesz; Elf32_Word p_memsz; Elf32_Word p_flags; Elf32_Word p_align; }; [Rationale: PT_LOAD=1 map file to mem vaddr p_vaddr size p_memsz (zero pad if >filesz), align p_align=0x1000 page; p_flags=PF_X=1 exec, PF_W=2 write, PF_R=4 read; 2.6.20: No PT_GNU_STACK (stack exec)]
          | | [e.g., phdr[0] PT_LOAD p_type=1 p_vaddr=0x8048000 p_offset=0x34 p_filesz=0x1000 p_memsz=0x1000 p_flags=5 (R/X); phdr[1] PT_LOAD data p_vaddr=0x804A000 p_offset=0x1034 p_filesz=0x200 p_memsz=0x800 (bss zero); phdr[2] PT_INTERP p_type=3 p_offset=0x1234 p_filesz=13 p_vaddr=0 p_align=1 "/lib/ld.so"; num_ph=2 loadable]
          |
          +--> VMA Mapping & Load (Line ~250): retval = elf_map(bprm->file, load_bias, elf_phdata, elf_ex.e_phnum, load_addr, &elf_interpreter); if (retval < 0) return retval; // elf_map: for each PT_LOAD phdr, vma = find_vma for vaddr, if no vma vma = do_brk(vaddr - load_bias, phdr->p_memsz, 0); map file vma->vm_start = phdr->p_vaddr - load_bias; kernel_read(file, phdr->p_offset, vma->vm_start, phdr->p_filesz); memset(vma->vm_start + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz); vma->vm_flags = calc_vm_flags(phdr->p_flags) | VM_EXEC; install_exec_creds(); // Set UID=0 capabilities
          | |
          | +--> Sub-Func elf_map (binfmt_elf.c Line ~450-550): unsigned long load_bias = ELF_ET_DYN_BASE - interp_load_addr if dynamic; for (int i=0; i<elf_ex.e_phnum; i++) { struct elf_phdr *phdr = elf_phdata + i; if (phdr->p_type != PT_LOAD) continue; unsigned long vaddr = phdr->p_vaddr - load_bias; unsigned long size = phdr->p_memsz; if (phdr->p_filesz > size) size = phdr->p_filesz; if (phdr->p_memsz > size) size = phdr->p_memsz; retval = map_pages(&current->mm, vaddr, size, phdr->p_flags); if (retval <0) return retval; if (phdr->p_filesz >0) { retval = kernel_read(bprm->file, phdr->p_offset, (char *)vaddr, phdr->p_filesz); if (retval <0) return retval; if (phdr->p_filesz < phdr->p_memsz) memset((char *)vaddr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz); } } // Map VMA for each load seg, read file to vaddr, zero bss
          | | [Rationale: Load segments to VMA (text RX, data RW); load_bias=0 for ET_EXEC fixed; map_pages = do_mmap(bprm->file, vaddr, size, prot=calc_vm_prot(phdr->p_flags), flags=MAP_FIXED|MAP_PRIVATE, phdr->p_offset); 2.6.20: ELF_ET_DYN_BASE=0x400000 for PIC, but ET_EXEC fixed 0x8048000; global unsigned long elf_map; error: do_mmap -ENOMEM -> "Out of memory" -ENOMEM]
          | | [Global: unsigned long load_addr = 0x8048000; [default ET_EXEC base; if ET_DYN load_bias=0x400000; struct elf_info {unsigned long load_bias; unsigned long dynamic_addr; ... }; Rationale: Fixed load for exec (no ASLR early, added 2.6.12 but opt); calc_vm_prot: PF_R=VM_READ, PF_W=VM_WRITE, PF_X=VM_EXEC; 2.6.20: No ET_REL (relocatable)]
          | | [e.g., phdr[0] text vaddr=0x8048000 size=0x1000 flags=5 R/X; do_mmap maps vma vm_start=0x8048000 vm_end=0x8049000 vm_flags=VM_READ|VM_EXEC|VM_MAYREAD|VM_MAYEXEC; kernel_read copies 0x1000B from file off=0x34 to 0x8048000; memset bss if memsz>filesz; phdr[1] data vaddr=0x804A000 size=0x800 flags=6 R/W; success retval=0]
          |
          +--> Interpreter Load (If Dynamic; Line ~280): if (elf_interpreter) { retval = load_elf_interp(elf_interpreter, bprm, &interp_load_addr); if (retval <0) return retval; load_bias = interp_load_addr - ELF_INTERPRETER_BASE; } // load_elf_interp: open_exec(interp "/lib/ld-linux.so.2"), load_elf_binary(interp, bprm interp)
          | |
          | +--> Sub-Func load_elf_interp (binfmt_elf.c Line ~600-650): struct file *interp_file = open_exec(elf_interpreter); if (IS_ERR(interp_file)) return PTR_ERR(interp_file); retval = load_elf_binary(interp_file, bprm, &interp_load_addr, interp_elf_ex.e_phnum, ELF_INTERPRETER_BASE); fput(interp_file); // Recursive load ld.so as interpreter (fixed base 0x400000)
          | | [Rationale: Dynamic ELF needs ld.so loader (reloc libs); elf_interpreter from PT_INTERP phdr "/lib/ld-linux.so.2"; 2.6.20: ELF_INTERPRETER_BASE=0x400000; global char elf_interpreter[128]; error: Open interp -ENOENT -> "No dynamic interpreter" -ENOEXEC]
          | | [Global: unsigned long interp_load_addr; [Rationale: ld.so maps at fixed base, sets up env, jumps to main e_entry; 2.6.20: No PIE (position independent exec) default; recursive but shallow (ld.so static? No, ld.so dynamic but chain short)]
          | | [e.g., PT_INTERP phdr p_filesz=13 "/lib/ld-linux.so.2\0"; open_exec -> interp_file; load_elf_binary(interp_file, bprm interp_phdata) maps ld.so text 0x400000; interp_load_addr=0x400000; success, ld.so sets argv/envp, dlopen libc, call init e_entry]
          |
          +--> Regs & MM Setup (Line ~290): current->mm = bprm->mm; set_binfmt(&current->mm->context, &elf_exec_fops); // New mm from bprm; set_regs(&bprm->regs, interp_load_addr + ehdr.e_entry, bprm, elf_ex); dump_thread(&bprm->regs, 0); // set_regs: bprm->regs.eip = entry; bprm->regs.esp = bprm->p - sizeof(void *); ((unsigned long *)bprm->regs.esp)[-1] = bprm->regs.esp; // Stack argc argv[0] NULL envp[0]... NULL NULL; dump_thread: sets TS_POLLING (FPU? No, thread state)
          | |
          | +--> Sub-Func set_regs (binfmt_elf.c Line ~700-750): struct pt_regs *regs = &bprm->regs; regs->cs = __USER_CS; regs->ss = __USER_DS; regs->ds = __USER_DS; regs->es = __USER_DS; regs->fs = 0; regs->gs = 0; regs->eflags = 0x200; regs->eip = entry (ehdr.e_entry + load_bias); regs->esp = bprm->p; // Virt stack top; copy_strings_kernel(1, &argv, bprm); copy_strings_kernel(bprm->envc, envp, bprm); // Push argc, argv[], NULL, envp[], NULL to stack
          | | [Rationale: Set user regs (CS=0x23 user code, DS=0x2B user data); eip=ELF entry (main or ld.so); esp=stack top (argc at esp, argv at esp+4, envp end+8); 2.6.20: __USER_CS=0x23, __USER_DS=0x2B; eflags=0x200 IF=1 interrupts; error: Bad entry (0) -> segfault on exec]
          | | [Global: unsigned long __USER_CS = 0x23, __USER_DS = 0x2B; [in asm-i386/segment.h; user segs DPL3; Rationale: Switch to user mode (ring3); 2.6.20: No x86_64 long; copy_strings_kernel pushes strings down stack (esp -= len; memcpy)]
          | | [e.g., entry=0x80482E0 (_start main); esp=0xBFFFFE00; copy_strings: esp -= 4*argc + 4*(argc+1) for argv NULL + envc; ((char **)esp)[argc] = NULL; for (i=0; i<argc; i++) { len = strlen(argv[i])+1; esp -= len; memcpy(esp, argv[i], len); argv_ptrs[i] = esp; } argv_ptrs at esp+4*argc; similar envp; 2.6.20: MAX_ARG_STRLEN=32 pages (128KB); dump_thread(regs, 0) sets regs->orig_ax=0 for syscall? No, thread dump for core]
          |
          +--> 2.6.20 Specifics: bprm->buf[0-128] filename; elf_phdata kmalloc(e_phnum*32B); load_bias=0 ET_EXEC; map_pages = do_mmap_pgoff(file, vaddr, size, prot, MAP_FIXED|MAP_PRIVATE, off>>PAGE_SHIFT); no ASLR (randomize_va_space=0 default); globals exported current->mm (dup_mm from kernel mm)
          | |
          | +--> [Overall: ~900-950ms (open ~5ms, parse ~10ms, map ~50ms, regs ~5ms); silent; ERR: Mmap -ENOMEM -> "Out of memory" fallback; Rationale: Load PID1 user image (text/data/stack VMA, user regs); next switch_to phase 42]
    |
    v
PID 1 Fork/Context Switch (Phase 42: sched.c switch_to; ~950ms; [ASM/C]; sys_execve flush_old_exec; switch_to init_task to PID1; 2.6.20: No cgroups attach; signals enable)









PID 1 Fork/Context Switch
[sched.c switch_to; ~950ms; [ASM/C]; Line ~1300; protected mode; post-execve flush_old_exec in sys_execve; switches from kernel_init thread (temp PID1) to new /sbin/init image as true PID1 user-space; reparents orphans to PID1; enables full signals; globals: struct task_struct *current (init_task PID0 -> kernel_init PID1 -> init PID1), struct runqueue *rq (&runqueues[0]), struct pt_regs regs (from do_execve bprm->regs); 2.6.20: No cgroups attach (basic cpusets); signals enable (unblock SIGCHLD etc.); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1 RUNNING; post-phase 41 ELF load (new mm/reg); next /sbin/init run phase 43; output: None (silent switch, init prints login); error: Switch bad ESP -> #SS triple fault reboot; no reparent -> orphans lost (zombie PIDs)]
    |
    +--> Execve Syscall Entry (fs/exec.c Line ~1200): long sys_execve(const char *filename, char __user *const __user *argv, char __user *const __user *envp) [Called from run_init_process do_execve("/sbin/init", init_argv, init_envp); user but kernel mode]
          |
          +--> Bprm Setup & Flush Old: struct linux_binprm *bprm = copy_strings_kernel(1, &argv, bprm); // Already prepped in phase 40; flush_old_exec(bprm); // Close files, exit_mm, exit_fs, exit_files, acct_process, bprm->mm = current->mm = dup_mm(NULL); current->personality = bprm->interp_personality ?: current->personality; current->sas_ss_sp = current->sas_ss = 0; set_personality(PER_LINUX, bprm);
          | |
          | +--> flush_old_exec Detail (exec.c Line ~1300-1400): exit_mm(); // mmput(current->mm); current->mm = NULL; exit_files(current); // fput all fds; exit_fs(current); // put_files_struct; acct_process(); // Account exec; current->flags &= ~(PF_RANDOMIZE | PF_FORKNOEXEC | PF_KTHREAD | PF_NOFREEZE); current->flags |= PF_REEXEC; current->mm = bprm->mm; bprm->mm = NULL; // New mm from load
          | | [Rationale: Cleanup kernel thread state (no mm/files for user); 2.6.20: acct_process do_acct_process (write /var/account/pacct exec record); global struct mm_struct *current->mm; error: mmput ref leak -> OOM later; PF_REEXEC bit for execed (no fork noexec)]
          | | [Global: unsigned long personality = PER_LINUX; [in asm-i386/elf.h; PER_LINUX=0; Rationale: ELF personality (PER_SVR4=1 Solaris etc.); bprm->interp_personality = interp_flags & ~BPERM_FLAGS_EXEC; 2.6.20: No randomize_va_space ASLR default 0]
          | | [e.g., exit_mm: if (current->mm) mmput(current->mm); current->mm = bprm->mm (from ELF map); exit_files: __close_fd(current->files, 0); put_files_struct; success current->mm->start_code=0x8048000]
          |
          +--> Binary Load Success (Line ~1220): if (retval = search_binary_handler(bprm) < 0) { put_bprm(bprm); return retval; } // Already loaded ELF in phase 41; success retval=0
          | |
          | +--> [Rationale: Confirm load; bprm->did_exec = 1; install_exec_creds (set uid=0 cap); 2.6.20: No seccomp exec (added 2.6.12 basic); error: Handler -ENOEXEC -> fallback]
          |
          +--> Signal & Creds Setup (Line ~1230): install_exec_creds(bprm); // Set capabilities (cap_bset=~0, cap_inheritable=0); current->cap_effective = current->cap_permitted = current->cap_bset; current->cap_inheritable = 0; // Root cap full
          | |
          | +--> Sub-Func install_exec_creds (exec.c Line ~1500): if (bprm->cred_prepared) { /* Already */ } else { validate_creds(current->cred); struct cred *new = prepare_exec_creds(); if (bprm->e_uid != current_uid(new)) { new->uid = new->euid = new->suid = bprm->e_uid; new->gid = new->egid = new->sgid = bprm->e_gid; } commit_creds(new); } // Full cap for root exec
          | | [Rationale: Reset creds for exec (no inheritable cap); 2.6.20: Basic cred (uid=0 root); global struct cred *current->cred; error: prepare_creds -ENOMEM -> "install_exec_creds: out of mem" -ENOMEM]
          | | [Global: struct cred { uid_t uid, gid, suid, sgid, euid, egid; uid_t fsuid, fsgid; kernel_cap_t cap_inheritable, cap_permitted, cap_effective, cap_bset, cap_ambient; }; [in include/linux/cred.h; prepare_exec_creds = prepare_creds() dup; commit_creds overwrites current->cred; 2.6.20: No ambient cap (added 4.3)]
          | | [e.g., bprm->e_uid=0 (setuid file? No, /sbin/init 4755 root); new->uid=0; cap_effective = CAP_FULL_SET (~0 cap); success creds reset]
          |
          +--> 2.6.20 Specifics: bprm->buf[0-4] ELF magic check in binfmt_elf; no ET_DYN bias (fixed ET_EXEC); map_pages do_mmap MAP_FIXED (fail if overlap); no brk exec (stack only); globals exported current->mm->arg_start/end (for /proc/pid/maps)
          | |
          | +--> [Overall: ~870-900ms (open/parse ~10ms, map ~50ms, regs ~10ms); silent; ERR: Mmap -EACCES bad prot -> -EACCES fallback; Rationale: PID1 user ready (new mm, user regs CS=3 DS=2B, eip=entry); next switch_to phase 42]
    |
    v
PID 1 Fork/Context Switch (Phase 42: sched.c; ~950ms; [ASM/C]; sys_execve flush_old_exec switch_to; 2.6.20: Reparent orphans to PID1)










 /sbin/init Run (User Space)
[fs/exec.c do_execve success -> PID1 user mode; ~950ms+; [C/User]; SysV init (m SysVinit or Upstart opt); reads /etc/inittab for runlevels; spawns getty on ttys for login; executes /etc/rcN.d scripts for daemons (N=default 3 multiuser); globals: char *inittab (parsed lines id:runlevels:action:process), struct runlevel *runlevel (current N=3), pid_t *pidhash[NR_HASH]; 2.6.20: SysV init default (Upstart 0.3 opt, but SysV common); /etc/inittab id:1:initdefault:3 (default runlevel 3); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1 RUNNING user ring3 CS=0x23; post-phase 42 switch_to; end of kernel boot (handover complete); output: "Debian GNU/Linux 4.0 ... hda: ST380011A, ATA DISK drive" from init daemons, login: prompt on tty1; error: No inittab -> fallback /bin/sh; bad script exec -> "init: Id "S10sysklogd" respawning too fast" loop]
    |
    +--> PID1 User Mode Entry (_start in /sbin/init ELF) [Line ~ _start asm in crt1.S; user ring3; eip=ELF e_entry ~0x80482E0; esp=0xBFFFFE00 argc/argv/envp stack]
          |
          +--> C Runtime Init (libc startup): __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end); // Call main(argc=1, argv[0]="/sbin/init"); [Rationale: Libc setup (atexit, environ); 2.6.20: glibc 2.5; global char **environ = init_envp; error: Bad argc -> segfault #SE]
          | |
          | +--> main(int argc, char **argv) in /sbin/init.c: [SysV init main; parse argv (init [runlevel]); load inittab; Rationale: User-space PID1 daemon; 2.6.20: SysVinit 2.86 common; source /etc/init.d/functions; global char *runlevel = argv[1] or "3"]
          | | [e.g., argc=1 argv[0]="/sbin/init"; main: open("/dev/console", O_RDWR); dup2(0,1); dup2(0,2); load_inittab(); determine_runlevel(); run_scripts(); 2.6.20: No systemd; Upstart opt but rare]
          |
          +--> Inittab Load & Parse (init.c Line ~200-300): load_inittab(); // read /etc/inittab, parse lines "id:runlevels:action:process" into struct parsed_inittab
          | |
          | +--> Sub-Func load_inittab (internal Line ~400-450): FILE *fp = fopen("/etc/inittab", "r"); if (!fp) { log("No inittab found"); fallback_sh(); } while (fgets(line, 256, fp)) { parse_line(line, &parsed); if (valid) add_to_list(&parsed); } fclose(fp); // Parse "id:runlevels:action:process" e.g., "si::sysinit:/etc/init.d/rcS", "1:2345:respawn:/sbin/getty 38400 tty1"
          | | [Rationale: Inittab config for runlevels (1 single, 2 multi no net, 3 multi net, 4 X, 5 X gui, 6 reboot); 2.6.20: SysV format; global struct list_head inittab_list; struct parsed_inittab { char id[4]; char runlevels[11]; char action[11]; char process[256]; }; error: Parse fail (bad line) -> skip; no inittab -> /bin/sh emergency]
          | | [Global: struct init_table *initabl; [linked list; Rationale: respawn action restarts process if exit; 2.6.20: No systemd targets; log via openlog syslog]
          | | [e.g., parse "1:2345:respawn:/sbin/getty 38400 tty1" -> id="1", runlevels="2345", action="respawn", process="/sbin/getty..."; add_to_list for runlevel match; default "id:1:initdefault::3" runlevel=3]
          |
          +--> Runlevel Determine (Line ~350): determine_runlevel(); // From inittab "initdefault:3" or argv[1]; set current_runlevel = 3 (multiuser net)
          | |
          | +--> Sub-Func determine_runlevel (internal Line ~500-550): struct parsed_inittab *def = find_id("initdefault"); if (def) { current_runlevel = def->runlevels[0] - '0'; } else current_runlevel = 3; // Default 3; if (argv[1]) current_runlevel = atoi(argv[1]); log("Switching to runlevel %d", current_runlevel);
          | | [Rationale: Runlevel from default or param (init 3); 2.6.20: SysV runlevels 1-6; global int current_runlevel = 0; error: Bad atoi -> default 3; no def -> 3]
          | | [Global: int runlevel; [in initreq.h; Rationale: /etc/rcN.d/S10sysklogd for N=3; 2.6.20: No systemd; log "Entering runlevel 3" via syslog]
          | | [e.g., def->runlevels="3" -> current=3; argv[1]="s" single -> 1; 2.6.20: runlevel(8) tool sets via telinit]
          |
          +--> Runlevel Scripts Execute (Line ~400): execute_runlevel(current_runlevel); // Run /etc/rcN.d scripts (S* start, K* kill); for N=3 /etc/rc3.d/S10sysklogd start (syslogd), S20udev (udev), S40crond (cron), S55sshd (sshd)
          | |
          | +--> Sub-Func execute_runlevel (internal Line ~600-700): char rc_dir[32]; snprintf(rc_dir, sizeof(rc_dir), "/etc/rc%d.d", runlevel); DIR *dir = opendir(rc_dir); if (!dir) { log("No rc dir"); return; } struct dirent *ent; while ((ent = readdir(dir))) { if (ent->d_name[0] == '.') continue; if (strncmp(ent->d_name, "S", 1) == 0 || strncmp(ent->d_name, "K", 1) == 0) { int num = atoi(ent->d_name +1); if (num < 0 || num > 99) continue; char *cmd = malloc(strlen(rc_dir) + strlen(ent->d_name) + 8); sprintf(cmd, "%s/%s start", rc_dir, ent->d_name); if (S) system(cmd); else if (K) system(cmd " stop"); free(cmd); } } closedir(dir); // Sort by num (S10 before S20)
          | | [Rationale: SysV rc scripts for daemons (start/stop); S=sequential num order; 2.6.20: system() fork/exec /bin/sh -c cmd; global no; error: opendir -ENOENT no /etc/rc3.d -> "No runlevel scripts" fallback; exec fail -> log "Failed to start S10sysklogd"]
          | | [Global: char *script_argv[4] = {"/bin/sh", "-c", cmd, NULL}; [for system(); Rationale: /etc/rc3.d/S10sysklogd = symlink to /etc/init.d/sysklogd; 2.6.20: No upstart jobs; log via syslog]
          | | [e.g., dir /etc/rc3.d: S10sysklogd, S20udev, K01halt; for S10 num=10 cmd="/etc/rc3.d/S10sysklogd start" -> sh -c; success "syslogd started"; 2.6.20: rcS for boot (sysinit), rc.local last]
          |
          +--> Getty Spawn for Console (Line ~450): for (int i=1; i<=6; i++) { /* ttys 1-6 */ spawn_getty(i); } // /sbin/getty 38400 tty1 for login prompt
          | |
          | +--> Sub-Func spawn_getty (internal Line ~800-850): char tty[16]; snprintf(tty, sizeof(tty), "/dev/tty%d", i); struct parsed_inittab *getty = find_action("respawn", "getty", tty); if (getty) { pid = fork(); if (pid == 0) { setsid(); open(tty, O_RDWR); dup2(0,1); dup2(0,2); execl("/sbin/getty", "getty", "38400", tty, NULL); _exit(1); } } // Respawn getty on exit
          | | [Rationale: Multi-user login on ttys (tty1 console); 2.6.20: Inittab "1:2345:respawn:/sbin/getty 38400 console"; global pid_t getty_pids[6]; error: Fork -EAGAIN no mem -> "Too many processes" log skip tty; exec fail -> respawn loop]
          | | [Global: struct termios getty_termios; [for stty; Rationale: getty sets baud 38400, login prompt; 2.6.20: mingetty or agetty; /dev/console from phase 36 devpts]
          | | [e.g., fork PID=4 getty tty1; setsid new session; open /dev/tty1; dup fd 0/1/2; execl getty -> "Debian GNU/Linux 4.0 hda login: " prompt; 2.6.20: No systemd getty; respawn if exit]
          |
          +--> 2.6.20 Specifics: SysV init 2.86 (m SysVinit); inittab parse fgets loop; runlevels 1-6 (3 default net no X); rcN.d symlinks to /etc/init.d (S= start, K=kill); no cgroup init (basic cpusets); globals exported current_runlevel (sysctl kernel.ctrl-alt-del=0 reboot)
          | |
          | +--> [Overall: ~950ms+ ongoing (parse ~10ms, scripts ~500ms, getty ~50ms); output: "Switching to runlevel: 3" "Starting syslogd" "udevd started" "sshd: Server listening" login: prompt; ERR: No inittab -> /bin/sh "# " emergency; Rationale: User-space boot (daemons, login); kernel idle post-handover]
    |
    v
Parallel Steady State (Phase 44: Post-handover; ~950ms+; [C/ASM]; ongoing; kthreadd spawns pdflush/kswapd; IRQ handlers active; scheduler RR; VFS ops; 2.6.20: No cgroups; O(1) idle 95%; jiffies tick 4ms)








Parallel Steady State (Post-Handover)
[Post-phase 43 /sbin/init exec; ~950ms+; [C/ASM/Ongoing]; protected mode full kernel; after PID1 user handover, kernel enters steady state (idle ~99% CPU, services via IRQs/threads); kthreadd spawns daemons (pdflush writeback, kswapd OOM killer, ksoftirqd if overload, migration for SMP load balance); IRQ handlers active (timer jiffies, IDE I/O complete, kbd input); scheduler RR for RT/prio for normal (O(1) pick); VFS ops (open/read/write via syscalls); globals: struct task_struct *current (switches PID1 init -> daemons -> idle), struct runqueue *runqueues[NR_CPUS] (per-CPU tasks), unsigned long jiffies (ticks for timers/sched), wait_queue_head_t *workqueue (for keventd); 2.6.20: No cgroups v1 full (basic cpusets limit tasks), O(1) sched active_array[140] queues, no ksoftirqd (do_softirq poll in IRQ); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1 RUNNING user ring3 active; parallel all phases (idle yields); end of boot (steady ops); output: Syslogd "kernel: Linux version 2.6.20..." dmesg, udev "adding device hda" /dev nodes, sshd "Server listening"; error: No kthreadd spawns -> no pdflush (dirty mem OOM), stuck services (panic if init crash); rationale: Kernel runtime (services via threads/IRQs, sched balances, VFS syscalls)]
    |
    +--> kthreadd Spawns Daemons (kthread.c ~950ms+; [C]; async PID2 loop spawns ~10 threads; pdflush for writeback, kswapd for reclaim)
          |
          +--> Daemon Spawn Loop: while (!kthread_should_stop()) { while (!list_empty(&kthread_create_list)) { struct kthread_create_info *create = list_entry(kthread_create_list.next, struct kthread_create_info, list); list_del_init(&create->list); struct pt_regs regs = {0}; regs.bx = (unsigned long)create->threadfn; regs.cx = (unsigned long)create->data; regs.ip = (unsigned long)kernel_thread_helper; struct task_struct *p = do_fork(CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL); if (IS_ERR(p)) create->result = PTR_ERR(p); else { create->result = p; wake_up_process(p); } wake_up(&kthread_create_wait); } set_current_state(TASK_INTERRUPTIBLE); schedule(); } // Exit if stopped
          | |
          | +--> Sub-Func kthread_should_stop (internal Line ~300): return test_bit(KTHREAD_SHOULD_STOP, &current->flags); // Set by kthread_stop (kill daemon)
          | | [Rationale: Central spawn (e.g., pdflush = kthread_run(pdflushd, NULL, "pdflush")); 2.6.20: ~5 spawns early (keventd, pdflushd, kswapd); global struct list_head kthread_create_list (DECLARE_LIST_HEAD); struct kthread_create_info { list_head list; int (*threadfn)(void *); void *data; struct task_struct *result; struct completion *done; }; error: do_fork -EAGAIN no mem -> result=-EAGAIN wake caller fail spawn (e.g., no pdflush dirty OOM)]
          | | [Global: struct task_struct *kthreadd_task = current; [Rationale: Find for kthread_stop (kill -TERM PID2); 2.6.20: No per-CPU kworkers (single daemon); kernel_thread_helper ASM: movl %ebx,%eax (arg); CALL *%esi (fn); mov $0x0B,%eax; int $0x80 (exit(0))]
          | | [e.g., kthread_run(keventd_main, NULL, "keventd") -> add to list, wake kthreadd; spawn PID3 keventd loop workqueue; 2.6.20: keventd_run: while (!kthread_should_stop()) { run_workqueue(keventd_wq); schedule_timeout_interruptible(10*HZ); } (poll hotplug)]
          |
          +--> IRQ Handlers Active (~950ms+; [C/ASM]; post-sti in idle; timer IRQ0 jiffies++, kbd IRQ1 input, IDE IRQ14 I/O end_request)
          | |
          | +--> do_IRQ Entry (arch/i386/kernel/entry.S): ENTRY(asm_do_IRQ); pushl %ds; pushl %es; pushl %fs; pushl %gs; pushl %eax; movl %esp,%edx; call do_IRQ; popl %eax; popl %gs; ... iret; // Common IRQ stub
          | | [Rationale: IRQ vec from IDT (0x20 timer); do_IRQ(int irq, struct pt_regs *regs): struct irq_desc *desc = irq_desc + irq; if (!desc->action) return; desc->action->flags |= SA_INTERRUPT; action = desc->action; action->handler(irq, desc->dev_id, regs); // Handler per IRQ
          | | [Global: struct irq_desc irq_desc [NR_IRQS=224]; [in irq.h; struct { struct list_head action; irq_flow_handler_t handler; unsigned int flags; cpumask_t affinity; ... }; Rationale: Per-IRQ desc (request_irq adds action); 2.6.20: SA_INTERRUPT=0x2000 mask IRQ in handler; error: No action -> spurious IRQ log ignore]
          | | [e.g., IRQ0 timer: desc->handler = handle_IRQ_event -> do_timer_interrupt (vector 0x20) -> do_timer(1) jiffies++; update_wall_time; check_preempt_curr; 2.6.20: PIT handler = timer_interrupt; IDE IRQ14: ide_intr (poll status, end_request if DRQ=0)]
          |
          +--> Scheduler Ongoing (O(1) RR/Prio; ~950ms+; [C]; pick_next_task scan bitmap, switch_to ASM esp/eip)
          | |
          | +--> Ongoing Schedule: In timer IRQ or syscall, if time_slice expire set_need_resched(); schedule() pick_next (highest prio bit), switch_to save/load esp
          | | [Rationale: Balance tasks (RR RT slice=100ms, prio normal); 2.6.20: O(1) ffz(bitmap) ~ for highest prio; global struct runqueue runqueues[NR_CPUS]; error: Bad switch_to -> #SS #PF crash]
          | | [Global: unsigned long time_slice = HZ / (1000 / HZ); [RT slice HZ/10=25ms; normal HZ/100=2.5ms; Rationale: Fair share; 2.6.20: No CFS fair; printk "sched: time slice %lu" none]
          |
          +--> VFS Syscalls Active (~950ms+; [C]; open/read/write via sys_open etc.; VFS caches used)
          | |
          | +--> sys_open Example (fs/open.c Line ~500): long sys_open(const char __user *filename, int flags, int mode) { struct file *file = do_filp_open(AT_FDCWD, filename, &opendata, flags); if (IS_ERR(file)) return PTR_ERR(file); fd = get_unused_fd(); if (fd >=0) { fd_install(fd, file); } else fput(file); return fd; } // VFS entry
          | | [Rationale: User syscalls (open("/etc/passwd", O_RDONLY) -> do_dentry_open -> f_op->read); 2.6.20: dentry lookup d_hash, inode iget from cache; global struct file_operations ext3_file_operations; error: No fd -> -EMFILE too many files]
          | | [Global: struct vfsmount *root_mnt; [from phase 36; Rationale: VFS ops on root /dev/sda1 ext3; 2.6.20: No fanotify; printk "VFS: file %s fd %d" if debug]
          |
          +--> 2.6.20 Specifics: kthreadd spawns ~5 early (keventd PID3, pdflushd PID4/5, kswapd PID6); no ksoftirqd (do_softirq in IRQ if overload); IRQ do_IRQ entry.S common stub push segs call C; scheduler no rt_mutex (prio lock); VFS no iomap (legacy block I/O)
          | |
          | +--> [Overall: ~950ms+ ongoing (threads ~100ms spawn, IRQs ~4ms/tick, sched ~us/switch); output: dmesg "udevd[45]: starting version 085" from init daemons; ERR: No pdflush -> dirty mem OOM panic; Rationale: Runtime kernel (services IRQ/thread, sched balance, VFS syscall); end boot (login prompt)]




