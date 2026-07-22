#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

/* XXX: This will ensure that cpu core is healthy
	where this watchdog is running but there
	mighjt be the case that C0 only running 
	the watchdog and C1, C2, C3 are frozen.

	or false positive c1, c3, c3 are frozen,
	even they are doing multi processes 
	scheduling but the watchdog never run on them

	so we have multicore watchdog stretegy
*/
int main(int argc, const char *argv[]) {
	int fd = open("/dev/watchdog", O_WRONLY);
	if (fd == -1) {
		perror("watchdog");
		exit(1);
	}
	while (1) {
		write(fd, "\0", 1);
		fsync(fd);
		sleep(10);
	}
}
