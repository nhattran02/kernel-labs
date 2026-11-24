// SPDX-License-Identifier: GPL-2.0
/* dma_test - data path through the edu DMA engine (Lab 19). */
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "uapi/lfd_edu.h"
#include "lfd_test.h"

int main(int argc, char **argv)
{
	struct lfd_edu_info info = { 0 };
	unsigned char a[4096], b[4096], c[300];
	unsigned int n, i;
	int fd, ok;

	fd = open(argc > 1 ? argv[1] : "/dev/edu0", O_RDWR);
	CHECK(fd >= 0, "open /dev/edu0");
	if (fd < 0)
		return lfd_summary();
	CHECK(ioctl(fd, LFD_EDU_GET_INFO, &info) == 0, "GET_INFO");
	CHECK(info.dma_mask == 0x0fffffff, "DMA mask is 28 bits (%#llx)",
	      (unsigned long long)info.dma_mask);
	n = info.dma_buf_size;
	CHECK(n == 4095 || n == 4096, "device buffer: %u usable bytes", n);
	if (n < 4095 || n > 4096)
		return lfd_summary();

	for (i = 0; i < n; i++)
		a[i] = (unsigned char)(i * 31 + 5);
	CHECK(pwrite(fd, a, n, 0) == (ssize_t)n, "DMA %u bytes RAM -> device", n);
	memset(b, 0, sizeof(b));
	CHECK(pread(fd, b, n, 0) == (ssize_t)n, "DMA %u bytes device -> RAM", n);
	CHECK(!memcmp(a, b, n), "the data came back intact");

	memset(c, 0xee, 100);
	CHECK(pwrite(fd, c, 100, 1000) == 100, "DMA 100 bytes to offset 1000");
	CHECK(pread(fd, c, 300, 900) == 300, "DMA 300 bytes from offset 900");
	ok = !memcmp(c, a + 900, 100);
	for (i = 100; i < 200; i++)
		ok &= c[i] == 0xee;
	ok &= !memcmp(c + 200, a + 1100, 100);
	CHECK(ok, "partial transfers land at the right offsets");

	CHECK(pread(fd, b, 10, n) == 0, "read at the end of the buffer is EOF");
	CHECK_ERR(pwrite(fd, b, 10, n), ENOSPC, "write at the end is ENOSPC");
	CHECK(pwrite(fd, b, 10, n - 4) == 4, "write straddling the end is short (4)");
	CHECK_ERR(pwrite(fd, LFD_BAD_PTR, 10, 0), EFAULT, "bad user pointer is EFAULT");
	close(fd);
	return lfd_summary();
}
