there are fixed regions where
vmalloc, kmalloc, ioremap etc works
and there is no KERNEL_BASE for whole kva like the xv6, to get kva -> pa
mapping.
