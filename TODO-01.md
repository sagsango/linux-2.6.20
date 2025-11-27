# kernel boot sequence
[GRUB or LILO]
    │
    ├─ loads kernel (bzImage) to 0x100000 (1 MB)
    ├─ sets up registers (EAX = magic, EBX = boot_params)
    └─ jumps to real-mode entry of kernel
         │
         ▼
arch/i386/boot/setup.S
         │
         ▼
arch/i386/boot/compressed/head.S   ← decompresses the kernel
         │
         ▼
arch/i386/kernel/head.S            ← **protected-mode kernel entry**
         │
         ▼
start_kernel() [init/main.c]

