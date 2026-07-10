##### This is Boot time device discovery flow (PCI Scan etc) #####
# Hardware discovery
 [pcibios_init()]  <-- System boot trigger
        │
        ▼
 [pci_scan_child_bus()]  <-- Starts loops for Buses 0-255
        │
        ▼
 [pci_scan_slot()]  <-- Loops for Devices 0-31
        │
        ▼
 [pci_bus_read_config_dword()]  <-- Reads hardware Vendor ID register
        │
 ┌──────┴────────────────────────┐
 ▼                               ▼
[Returns 0xFFFFFFFF]       [Returns Real ID]
(Empty Slot -> Skip)       (Hardware Discovered!)
                                 │
                                 ▼
                           [device_add()]  <-- Registers inside sysfs
                                 │
                                 ▼
                     [__pci_device_probe()] <-- Knocks on your storage driver's door



# device add:
/* drivers/base/dd.c (Linux 2.6.20) */
int device_attach(struct device *dev)
{
    // If the device is already tied to a driver, skip
    if (dev->driver)
        return 1;

    // Loop through every driver registered on this specific bus type
    return bus_for_each_drv(dev->bus, NULL, dev, __device_attach);
}


# check for the driver:
/* drivers/base/dd.c (Linux 2.6.20) */
int device_attach(struct device *dev)
{
    // If the device is already tied to a driver, skip
    if (dev->driver)
        return 1;

    // Loop through every driver registered on this specific bus type
    return bus_for_each_drv(dev->bus, NULL, dev, __device_attach);
}


# driver found:
/* drivers/base/dd.c */
static int __device_attach(struct device_driver *drv, void *data)
{
    struct device *dev = data;

    // Check if this specific driver can handle this specific device
    if (drv->bus->match && !drv->bus->match(dev, drv))
        return 0; // Not a match, continue the loop to the next driver

    // If it passes the match test, attempt to bind them
    return driver_probe_device(drv, dev);
}



# final flow:
[ Physical PCI Bus Scan ]
          │
          ▼
   pci_scan_slot()          <-- Discovers device at physical address
          │
          ▼
   device_add()             <-- Instantiates generic kernel device object
          │
          ▼
   device_attach()          <-- Starts the driver lookup phase
          │
          ▼
   bus_for_each_drv()       <-- Iterates through the list of loaded drivers
          │
          ▼
   __device_attach()        <-- Invokes pci_bus_match() to check Vendor/Device IDs
          │
          ▼
   driver_probe_device()    <-- Setup phase for matching pair
          │
          ▼
   pci_device_probe()       <-- Casts generic device to PCI structure types
          │
          ▼
  __pci_device_probe()      <-- [YOUR FUNCTION] Officially binds them together
          │
          ▼
   drv->probe()             <-- Runs your custom driver (calls add_dis
