```text
===============================================================================
IO-APIC INTERRUPT ROUTING
File: arch/x86_64/kernel/io_apic.c
===============================================================================

PURPOSE
=======

This file programs the IO-APIC.

Its job is to route external device interrupts to CPUs.

Device IRQ
    |
    v
IO-APIC Pin
    |
    v
IO-APIC Redirection Entry
    |
    v
Local APIC
    |
    v
CPU Vector
    |
    v
IDT
    |
    v
Linux IRQ Handler


===============================================================================
BACKGROUND
==========

Old PC interrupt path:

Device
  |
  v
8259 PIC
  |
  v
CPU


Modern SMP interrupt path:

Device
  |
  v
IO-APIC
  |
  v
Local APIC
  |
  v
CPU


The IO-APIC is external to the CPU.

The Local APIC is inside each CPU.


===============================================================================
MAIN RESPONSIBILITIES
===============================================================================

io_apic.c handles:

    1. IO-APIC MMIO access

    2. IRQ -> IO-APIC pin mapping

    3. IO-APIC redirection table setup

    4. interrupt vector allocation

    5. interrupt CPU affinity

    6. timer IRQ0 routing workarounds

    7. MSI / HyperTransport interrupt setup

    8. IO-APIC suspend/resume


===============================================================================
IO-APIC HARDWARE MODEL
===============================================================================

Each IO-APIC has pins.

Example:

IO-APIC 0

    pin 0   -> timer
    pin 1   -> keyboard
    pin 2   -> cascade
    pin 16  -> PCI device
    pin 17  -> PCI device
    pin 18  -> PCI device


Each pin has a redirection table entry.

Redirection entry contains:

    vector
    destination CPU/APIC
    trigger mode
    polarity
    mask bit
    delivery mode


===============================================================================
IO-APIC MMIO ACCESS
===============================================================================

Important functions:

    io_apic_read()
    io_apic_write()
    io_apic_modify()
    io_apic_sync()


IO-APIC registers are accessed indirectly:

    write register number to index register
    read/write value through data register


Flow:

io_apic_write(apic, reg, value)
      |
      +--> io_apic_base(apic)
      |
      +--> write reg to index
      |
      +--> write value to data


===============================================================================
IO-APIC BASE ADDRESS
===============================================================================

Function:

    io_apic_base(idx)


Uses:

    FIX_IO_APIC_BASE_0 + idx


Meaning:

    IO-APIC physical MMIO is mapped into kernel fixmap area.


Flow:

Physical IO-APIC MMIO
      |
      v
fixmap virtual address
      |
      v
kernel can read/write registers


===============================================================================
REDIRECTION TABLE ENTRY
===============================================================================

Kernel structure:

    struct IO_APIC_route_entry


Fields:

    vector
    delivery_mode
    dest_mode
    polarity
    trigger
    mask
    destination


Example:

IRQ16 redirection entry:

    vector        = 0x51
    destination   = CPU0
    trigger       = level
    polarity      = low
    mask          = 0
    delivery_mode = fixed


===============================================================================
READING REDIRECTION ENTRY
===============================================================================

Function:

    ioapic_read_entry(apic, pin)


Each entry is two 32-bit registers:

    low word  = 0x10 + 2 * pin
    high word = 0x11 + 2 * pin


Flow:

read low word
read high word
combine into route entry


===============================================================================
WRITING REDIRECTION ENTRY
===============================================================================

Function:

    ioapic_write_entry(apic, pin, entry)


Important rule:

    Write high word first.
    Then write low word.


Why?

The low word contains the mask bit.

If low word unmasks the interrupt before high word is valid,
interrupt could fire with incomplete destination.


===============================================================================
MASKING REDIRECTION ENTRY
===============================================================================

Function:

    ioapic_mask_entry(apic, pin)


Important rule:

    Write low word first.


Why?

To set mask bit immediately before changing high bits.


===============================================================================
IRQ TO PIN MAP
===============================================================================

Structure:

    struct irq_pin_list {
        short apic;
        short pin;
        short next;
    };


Array:

    irq_2_pin[]


Purpose:

    Map Linux IRQ number to IO-APIC pin(s).


Common case:

    IRQ16 -> APIC0 pin16


Shared case:

    IRQ16 -> APIC0 pin16
          -> APIC1 pin3


===============================================================================
add_pin_to_irq()
===============================================================================

Purpose:

    Add an IO-APIC pin to an IRQ.


Flow:

add_pin_to_irq(irq, apic, pin)
      |
      +--> find existing irq_2_pin entry
      |
      +--> if empty, fill it
      |
      +--> if already used, allocate chained entry
      |
      v
IRQ knows which IO-APIC pin controls it


===============================================================================
MASK / UNMASK IO-APIC IRQ
===============================================================================

Functions:

    mask_IO_APIC_irq()
    unmask_IO_APIC_irq()


They modify the mask bit in every IO-APIC pin mapped to that IRQ.


mask bit:

    1 = interrupt disabled
    0 = interrupt enabled


Flow:

mask_IO_APIC_irq(irq)
      |
      v
for each mapped APIC pin:
      |
      v
set mask bit


===============================================================================
CLEAR IO-APIC
===============================================================================

Function:

    clear_IO_APIC()


Purpose:

    Mask all IO-APIC pins.


Used during:

    boot setup
    reboot
    crash shutdown


It avoids leaving stale interrupt routing active.


===============================================================================
BOOT OPTIONS
===============================================================================

noapic
------

Sets:

    skip_ioapic_setup = 1


Meaning:

    Do not use IO-APIC.


disable_8254_timer
------------------

Disables timer-over-8254 path.


enable_8254_timer
-----------------

Forces timer-over-8254 path.


no_timer_check
--------------

Skip timer IRQ verification.


===============================================================================
MP TABLE / ACPI ROUTING
===============================================================================

The file uses firmware IRQ routing info.

Sources:

    MP table
    ACPI MADT


Important functions:

    find_irq_entry()
    find_isa_irq_pin()
    find_isa_irq_apic()
    IO_APIC_get_PCI_irq_vector()


They answer:

    Which IO-APIC pin is connected to this IRQ/device?


===============================================================================
ISA IRQ ROUTING
===============================================================================

ISA IRQs are usually:

    edge triggered
    high active


Example:

    IRQ0 timer
    IRQ1 keyboard
    IRQ12 mouse


Defaults:

    trigger  = edge
    polarity = high


===============================================================================
PCI IRQ ROUTING
===============================================================================

PCI interrupts are usually:

    level triggered
    low active


Defaults:

    trigger  = level
    polarity = low


===============================================================================
pin_2_irq()
===============================================================================

Purpose:

    Convert IO-APIC pin to Linux IRQ number.


ISA case:

    IRQ = source bus IRQ


PCI case:

    IRQ is computed based on APIC number and pin number.


Flow:

MP table entry
      |
      v
source bus / pin
      |
      v
Linux IRQ number


===============================================================================
INTERRUPT VECTOR ALLOCATION
===============================================================================

Important arrays:

    irq_vector[irq]

        IRQ -> CPU vector


    irq_domain[irq]

        CPUs where this vector is valid


    per_cpu(vector_irq, cpu)[vector]

        CPU vector -> IRQ


===============================================================================
assign_irq_vector()
===============================================================================

Purpose:

    Assign CPU interrupt vector for an IRQ.


Example:

    IRQ16 -> vector 0x51


Why not use IRQ number directly?

Because CPU receives interrupt vectors, not Linux IRQ numbers.


Flow:

assign_irq_vector(irq, cpu_mask)
      |
      +--> check existing vector
      |
      +--> find free vector
      |
      +--> avoid IA32_SYSCALL_VECTOR
      |
      +--> spread vectors by priority level
      |
      +--> update per_cpu vector_irq[]
      |
      +--> save irq_vector[irq]
      |
      v
return vector


===============================================================================
WHY VECTOR += 8?
===============================================================================

APIC priority level is based on high bits of vector.

If vectors are too close:

    0x50
    0x51
    0x52

they are in same priority group.

Linux spreads them:

    0x50
    0x58
    0x60

to avoid too many interrupts at one APIC priority level.


===============================================================================
SETUP IO-APIC IRQ
===============================================================================

Function:

    setup_IO_APIC_irq(apic, pin, idx, irq)


Purpose:

    Build and write one redirection table entry.


Flow:

setup_IO_APIC_irq()
      |
      +--> create empty route entry
      |
      +--> set delivery mode
      |
      +--> set destination mode
      |
      +--> set trigger/polarity
      |
      +--> assign vector
      |
      +--> register Linux IRQ handler type
      |
      +--> disable legacy PIC IRQ if needed
      |
      +--> write IO-APIC entry
      |
      +--> save IRQ affinity info


===============================================================================
EDGE VS LEVEL HANDLING
===============================================================================

Edge interrupt:

    one pulse
    handled by handle_edge_irq


Level interrupt:

    line stays asserted until device clears condition
    handled by handle_fasteoi_irq


Why important?

Using wrong handler can cause:

    lost interrupts
    IRQ storms


===============================================================================
REGISTER IRQ HANDLER TYPE
===============================================================================

Function:

    ioapic_register_intr()


If level-triggered:

    irq chip = ioapic_chip
    handler  = handle_fasteoi_irq


If edge-triggered:

    irq chip = ioapic_chip
    handler  = handle_edge_irq


===============================================================================
SETUP ALL IO-APIC IRQs
===============================================================================

Function:

    setup_IO_APIC_irqs()


Flow:

for each IO-APIC:
    for each pin:
        find firmware IRQ entry
        if connected:
            irq = pin_2_irq()
            add_pin_to_irq()
            setup_IO_APIC_irq()
        else:
            print not connected


===============================================================================
IOAPIC CHIP
===============================================================================

Structure:

    ioapic_chip


Operations:

    startup
    mask
    unmask
    ack
    eoi
    set_affinity
    retrigger


Generic IRQ layer calls these.


===============================================================================
ACK EDGE IRQ
===============================================================================

Function:

    ack_apic_edge()


Flow:

    move_native_irq()
    ack_APIC_irq()


Meaning:

    Maybe move IRQ affinity.
    Then send EOI to Local APIC.


===============================================================================
ACK LEVEL IRQ
===============================================================================

Function:

    ack_apic_level()


Flow:

    if IRQ move pending:
        mask IO-APIC IRQ

    ack_APIC_irq()

    move_masked_irq()

    if masked for move:
        unmask IO-APIC IRQ


Why?

Level interrupts must be carefully masked while moving affinity,
or the line may retrigger on wrong CPU.


===============================================================================
IO-APIC AFFINITY
===============================================================================

Function:

    set_ioapic_affinity_irq()


Purpose:

    Move IRQ to another CPU set.


Flow:

set_ioapic_affinity_irq(irq, mask)
      |
      +--> intersect mask with online CPUs
      |
      +--> assign vector for new CPU domain
      |
      +--> convert CPU mask to APIC destination
      |
      +--> rewrite IO-APIC route entry
      |
      +--> update IRQ affinity info


===============================================================================
ENABLE IO-APIC
===============================================================================

Function:

    enable_IO_APIC()


Flow:

enable_IO_APIC()
      |
      +--> clear irq_2_pin[]
      |
      +--> read number of pins per IO-APIC
      |
      +--> detect ExtINT pin for 8259
      |
      +--> compare with MP table
      |
      +--> clear all IO-APIC entries


===============================================================================
DISABLE IO-APIC
===============================================================================

Function:

    disable_IO_APIC()


Used during reboot/crash.

Flow:

disable_IO_APIC()
      |
      +--> clear IO-APIC entries
      |
      +--> if 8259 routed through IO-APIC:
      |       |
      |       +--> restore virtual wire ExtINT mode
      |
      +--> disconnect BSP APIC


Purpose:

    Leave machine in firmware-friendly interrupt state.


===============================================================================
TIMER IRQ0 CHECK
===============================================================================

Function:

    check_timer()


This is one of the ugliest but most important legacy paths.

Problem:

    Some BIOS/MP tables lie about timer IRQ0 routing.


Linux tries multiple methods.


===============================================================================
TIMER METHOD 1: IO-APIC PIN
===============================================================================

Try:

    IRQ0 through IO-APIC pin from firmware table


If timer ticks work:

    done


If not:

    clear pin and try fallback


===============================================================================
TIMER METHOD 2: 8259 THROUGH IO-APIC ExtINT
===============================================================================

Try:

    8259 PIC
       |
       v
    IO-APIC ExtINT pin
       |
       v
    Local APIC


Function:

    setup_ExtINT_IRQ0_pin()


If works:

    done


===============================================================================
TIMER METHOD 3: VIRTUAL WIRE IRQ
===============================================================================

Try:

    8259
      |
      v
    Local APIC LVT0 fixed vector


If works:

    done


===============================================================================
TIMER METHOD 4: ExtINT DIRECT
===============================================================================

Try:

    8259 ExtINT through LVT0


If all fail:

    panic()


Message:

    IO-APIC + timer doesn't work!
    Try using noapic


===============================================================================
TIMER CHECK FLOW
===============================================================================

check_timer()
      |
      +--> assign vector for IRQ0
      |
      +--> try IO-APIC timer pin
      |
      +--> if works: return
      |
      +--> try 8259 through IO-APIC ExtINT
      |
      +--> if works: return
      |
      +--> try virtual wire IRQ
      |
      +--> if works: return
      |
      +--> try ExtINT IRQ
      |
      +--> if works: return
      |
      +--> panic


===============================================================================
SETUP IO-APIC MAIN
===============================================================================

Function:

    setup_IO_APIC()


Flow:

setup_IO_APIC()
      |
      +--> enable_IO_APIC()
      |
      +--> decide which IRQs go through IO-APIC
      |
      +--> sync arbitration IDs
      |
      +--> setup IO-APIC IRQ entries
      |
      +--> initialize traps/fallbacks
      |
      +--> check timer
      |
      +--> print IO-APIC state


===============================================================================
SUSPEND / RESUME
===============================================================================

IO-APIC state can be lost across suspend.

Suspend:

    ioapic_suspend()
        save every redirection table entry


Resume:

    ioapic_resume()
        restore IO-APIC ID
        restore every redirection table entry


===============================================================================
DYNAMIC IRQ ALLOCATION
===============================================================================

Functions:

    create_irq()
    destroy_irq()


Used for dynamic interrupts such as MSI.


create_irq()
      |
      +--> find unused IRQ
      |
      +--> assign vector
      |
      +--> dynamic_irq_init()


destroy_irq()
      |
      +--> dynamic_irq_cleanup()
      |
      +--> clear vector mapping


===============================================================================
MSI SUPPORT
===============================================================================

Traditional interrupt:

Device
  |
  v
IO-APIC pin
  |
  v
CPU


MSI interrupt:

Device
  |
  v
PCI memory write
  |
  v
Local APIC
  |
  v
CPU vector


MSI bypasses IO-APIC pins.


===============================================================================
MSI MESSAGE
===============================================================================

Function:

    msi_compose_msg()


Builds:

    address_hi
    address_lo
    data


Contains:

    destination APIC ID
    destination mode
    delivery mode
    vector


Flow:

assign vector
      |
      v
choose APIC destination
      |
      v
compose MSI address/data
      |
      v
device writes message to trigger interrupt


===============================================================================
MSI SETUP
===============================================================================

Function:

    arch_setup_msi_irq()


Flow:

arch_setup_msi_irq()
      |
      +--> msi_compose_msg()
      |
      +--> write_msi_msg()
      |
      +--> set irq chip = PCI-MSI
      |
      +--> handler = handle_edge_irq


===============================================================================
HYPERTRANSPORT IRQ SUPPORT
===============================================================================

HT IRQs are message-based interrupts used on AMD/HyperTransport systems.

Similar idea to MSI:

Device
  |
  v
HT interrupt message
  |
  v
APIC vector


Function:

    arch_setup_ht_irq()


===============================================================================
COMPLETE BOOT FLOW
===============================================================================

start_kernel()
      |
      v
init_IRQ()
      |
      v
setup_IO_APIC()
      |
      +--> enable_IO_APIC()
      |
      +--> setup_IO_APIC_irqs()
      |
      +--> assign_irq_vector()
      |
      +--> ioapic_write_entry()
      |
      +--> check_timer()
      |
      v
external interrupt routing ready


===============================================================================
COMPLETE RUNTIME FLOW
===============================================================================

Network packet arrives
      |
      v
NIC asserts INTx
      |
      v
IO-APIC pin
      |
      v
redirection entry chooses:
      |
      +--> vector
      +--> destination CPU
      |
      v
Local APIC
      |
      v
CPU interrupt
      |
      v
IDT[vector]
      |
      v
entry.S IRQ stub
      |
      v
do_IRQ()
      |
      v
generic IRQ layer
      |
      v
driver interrupt handler


===============================================================================
RELATION TO OTHER FILES
===============================================================================

i8259.c / irq.c
---------------

Sets IDT gates for IRQ vectors and initializes legacy PIC.


apic.c
------

Programs Local APIC and handles APIC timer/IPI/error/spurious vectors.


genapic*.c
----------

Chooses APIC destination mode and converts CPU masks to APIC IDs.


entry.S
-------

Receives CPU vector and enters common interrupt path.


crash.c
-------

Calls disable_IO_APIC() before kexec crash kernel.


early-quirks.c
--------------

May change timer/APIC behavior before this file runs.


===============================================================================
KEY IDEA
===============================================================================

io_apic.c is the external interrupt router for x86 SMP.

It translates:

    physical device interrupt pins

into:

    CPU interrupt vectors

and programs:

    where the interrupt goes
    how it is triggered
    how it is acknowledged
    which CPU receives it


Without this file:

    PCI/ISA external interrupts would not be routed correctly
    SMP interrupt affinity would not work
    IO-APIC timer routing would fail
    MSI/HT interrupt vectors could not be allocated properly

This file is the bridge between hardware interrupt wiring and the Linux
generic IRQ subsystem. :contentReference[oaicite:0]{index=0}
===============================================================================
```

