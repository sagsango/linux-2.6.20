# Code
In Linux 2.6.20 the hotplug mechanism is the bridge between
kernel device-model events (new device appears / disappears)
and userspace helpers like /sbin/hotplug or, later, udevd.

That mechanism is built entirely inside the driver-core subsystem under:
drivers/base/


# The key files and functions
| File                      | Function                                                                      | Purpose                                                         |
| ------------------------- | ----------------------------------------------------------------------------- | --------------------------------------------------------------- |
| `drivers/base/core.c`     | `device_add()`, `device_del()`                                                | Register / unregister devices; these trigger hotplug events.    |
| `lib/kobject_uevent.c`    | `kobject_uevent()` / `kobject_uevent_env()`                                   | Core logic that actually sends the hotplug/uevent to userspace. |
| `kernel/kmod.c`           | `call_usermodehelper()`                                                       | Spawns `/sbin/hotplug` (the userspace helper).                  |
| `include/linux/kobject.h` | Declarations for uevent enums and prototypes.                                 |                                                                 |
| `net/netlink/` (later)    | Provides `NETLINK_KOBJECT_UEVENT` socket implementation for netlink delivery. |                                                                 |
|_____________________________________________________________________________________________________________________________________________________________________________|


# High-level flow in 2.6.20
Driver calls device_register(dev)
          ↓
device_add(dev)                         (drivers/base/core.c)
          ↓
kobject_uevent(&dev->kobj, KOBJ_ADD)    (lib/kobject_uevent.c)
          ↓
kobject_uevent_env()
   ├─ builds environment: ACTION=add, DEVPATH=/class/block/sda, ...
   ├─ tries to broadcast via netlink (if CONFIG_NET)
   └─ always calls kobject_uevent_exec()
          ↓
kobject_uevent_exec()
   └─ call_usermodehelper("/sbin/hotplug", argv, envp, UMH_WAIT_PROC)
          ↓
User mode: /sbin/hotplug → executes /sbin/udev


# Flow diagram 
               ┌────────────────────────────┐
               │   driver registers device  │
               │   device_add(dev)          │
               └────────────┬───────────────┘
                            │
                            ▼
                 kobject_uevent(KOBJ_ADD)
                            │
                ┌───────────┴────────────┐
                │ lib/kobject_uevent.c   │
                ├────────────────────────┤
                │ Build env[]:           │
                │   ACTION=add           │
                │   DEVPATH=/class/usb/... │
                │   SUBSYSTEM=usb         │
                ├────────────────────────┤
                │ Send over netlink (if enabled) │
                │ Call /sbin/hotplug via         │
                │ call_usermodehelper()          │
                └───────────┬────────────┘
                            │
                            ▼
                     /sbin/hotplug
                            │
                            ▼
                         /sbin/udev

