// SPDX-License-Identifier: GPL-2.0
/*
 * edu_test - acceptance test for Lab 18.
 *   edu_test [/dev/edu0]         functional checks
 *   edu_test gone /dev/edu0 N    open, sleep N s (unbind meanwhile), expect ENODEV
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "uapi/lfd_edu.h"
#include "lfd_test.h"

static int fd;
static int bad;

static void *fact_worker(void *arg)
{
	static const uint32_t expect[] = { 1, 1, 2, 6, 24, 120, 720, 5040, 40320, 362880 };
	long id = (long)arg;

	for (int i = 0; i < 50; i++) {
		uint32_t n = (id + i) % 10;

		if (ioctl(fd, LFD_EDU_FACT, &n) || n != expect[(id + i) % 10])
			__atomic_add_fetch(&bad, 1, __ATOMIC_RELAXED);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	struct lfd_edu_info info;
	pthread_t t[4];
	uint32_t n;
	long i;

	if (argc == 4 && !strcmp(argv[1], "gone")) {
		fd = open(argv[2], O_RDWR);
		if (fd < 0)
			return 2;
		sleep(atoi(argv[3]));
		n = 5;
		if (ioctl(fd, LFD_EDU_FACT, &n) == -1 && errno == ENODEV) {
			printf("ioctl after removal: ENODEV\n");
			close(fd);
			return 0;
		}
		printf("ioctl after removal: ret %u errno %d\n", n, errno);
		return 1;
	}

	fd = open(argc > 1 ? argv[1] : "/dev/edu0", O_RDWR);
	CHECK(fd >= 0, "open /dev/edu0");
	if (fd < 0)
		return lfd_summary();
	CHECK(ioctl(fd, LFD_EDU_GET_INFO, &info) == 0, "GET_INFO");
	CHECK((info.version & 0xff) == 0xed, "ID register %#x ends in 0xed", info.version);
	CHECK(info.bar_size == 1 << 20, "BAR0 is 1 MiB (%llu)", (unsigned long long)info.bar_size);
	DIAG("IRQ mode: %s", info.irq_mode == LFD_EDU_IRQ_MSI ? "MSI" : "INTx");
	n = 10;
	CHECK(ioctl(fd, LFD_EDU_FACT, &n) == 0 && n == 3628800, "10! = 3628800");
	n = 0;
	CHECK(ioctl(fd, LFD_EDU_FACT, &n) == 0 && n == 1, "0! = 1");
	for (i = 0; i < 4; i++)
		pthread_create(&t[i], NULL, fact_worker, (void *)i);
	for (i = 0; i < 4; i++)
		pthread_join(t[i], NULL);
	CHECK(bad == 0, "4 threads x 50 factorials, all correct (%d wrong)", bad);
	CHECK_ERR(ioctl(fd, _IO(0xF5, 0x7f)), ENOTTY, "unknown ioctl is ENOTTY");
	close(fd);
	return lfd_summary();
}
