// SPDX-License-Identifier: GPL-2.0
/* edu_fact N - compute N! through /dev/edu0 and print it (Lab 23). */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "uapi/lfd_edu.h"

int main(int argc, char **argv)
{
	unsigned int n = argc > 1 ? atoi(argv[1]) : 5;
	int fd = open("/dev/edu0", O_RDWR);

	if (fd < 0 || ioctl(fd, LFD_EDU_FACT, &n)) {
		perror("edu_fact");
		return 1;
	}
	printf("%u\n", n);
	return 0;
}
