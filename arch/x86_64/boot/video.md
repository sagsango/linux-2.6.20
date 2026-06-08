BIOS
 |
 v
bootsect
 |
 v
setup.S
 |
 +--> video.S
 |
 v
protected mode



What display adapter do I have?
What video modes exist?
What is the current screen size?
Can I switch to another mode?


===============================================================================
                            video()
===============================================================================

setup.S
    |
    | call video
    |
    v

+----------------------+
| video()              |
+----------------------+

    |
    v

basic_detect()

    |
    +---- MDA
    |
    +---- CGA
    |
    +---- EGA
    |
    +---- VGA

    |
    v

mode detection

    |
    +---- VGA modes
    |
    +---- VESA modes
    |
    +---- SVGA modes
    |
    +---- user selected mode

    |
    v

store parameters

    |
    +---- rows
    +---- columns
    +---- cursor position
    +---- framebuffer
    +---- color depth

    |
    v

return to setup.S

===============================================================================














===============================================================================
                       BOOT VIDEO INITIALIZATION
===============================================================================

setup.S
    |
    v

video()

    |
    +--> Detect adapter
    |       |
    |       +--> MDA
    |       +--> CGA
    |       +--> EGA
    |       +--> VGA
    |
    +--> Detect VESA
    |
    +--> Detect SVGA vendor
    |       |
    |       +--> ATI
    |       +--> S3
    |       +--> Cirrus
    |       +--> Trident
    |       +--> Tseng
    |
    +--> Build mode table
    |
    +--> Select mode
    |
    +--> Store parameters
    |
    v

boot parameter block
(0x90000 area)

    rows
    columns
    cursor
    framebuffer
    vesa info
    video type

    |
    v

head.S

    |
    v

start_kernel()

    |
    v

console_init()

    |
    v

Linux knows what display exists
===============================================================================
