We did not had ringbuffer with /dev/kmsg exposed to userspace in v2.6

kernel-2.6 ❱❱❱ find * | grep -i "kmsg"
fs/proc/kmsg.c
kernel-2.6 ❱❱❱ vim fs/proc/kmsg.c
kernel-2.6 ❱❱❱ grep -r "do_syslog"
fs/proc/kmsg.c:extern int do_syslog(int type, char __user *bug, int count);
fs/proc/kmsg.c:	return do_syslog(1,NULL,0);
fs/proc/kmsg.c:	(void) do_syslog(0,NULL,0);
fs/proc/kmsg.c:	if ((file->f_flags & O_NONBLOCK) && !do_syslog(9, NULL, 0))
fs/proc/kmsg.c:	return do_syslog(2, buf, count);
fs/proc/kmsg.c:	if (do_syslog(9, NULL, 0))
kernel/printk.c: * Commands to do_syslog:
kernel/printk.c:int do_syslog(int type, char __user *buf, int len)
kernel/printk.c:	return do_syslog(type, buf, len);
kernel-2.6 ❱❱❱ find * | grep -i "kmsg"
fs/proc/kmsg.c
