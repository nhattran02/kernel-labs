// SPDX-License-Identifier: GPL-2.0
/*
 * lfdregs_test - acceptance test for Lab 05.  Built as a 64-bit and as a
 * 32-bit program (lfdregs_test32); both must pass.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include "uapi/lfdregs.h"
#include "lfd_test.h"

/* Works whether the header declares buf as void * or as __u64. */
#define SET_BUF(b, p) ((b).buf = (__typeof__((b).buf))(uintptr_t)(p))

int main(void)
{
	struct lfdregs_info info = { 0 };
	struct lfdregs_reg r;
	struct lfdregs_bulk b;
	uint32_t vals[8];
	int fd, status;
	pid_t pid;

	DIAG("%zu-bit build: sizeof(struct lfdregs_bulk) = %zu, LFDREGS_BULK_READ = %#lx",
	     sizeof(long) * 8, sizeof(struct lfdregs_bulk), (unsigned long)LFDREGS_BULK_READ);

	fd = open("/dev/lfdregs", O_RDWR);
	CHECK(fd >= 0, "open /dev/lfdregs");
	if (fd < 0)
		return lfd_summary();

	CHECK(ioctl(fd, LFDREGS_GET_INFO, &info) == 0, "GET_INFO");
	CHECK(info.abi_version == LFDREGS_ABI_VERSION && info.nregs == LFDREGS_NREGS,
	      "GET_INFO reports ABI %u and %u registers", info.abi_version, info.nregs);

	r.index = 0;
	r.value = 0;
	CHECK(ioctl(fd, LFDREGS_READ, &r) == 0 && r.value == LFDREGS_ID,
	      "register 0 reads the ID %#x", LFDREGS_ID);
	r.value = 1;
	CHECK_ERR(ioctl(fd, LFDREGS_WRITE, &r), EPERM, "register 0 is read-only");

	r.index = 5;
	r.value = 0x1234;
	CHECK(ioctl(fd, LFDREGS_WRITE, &r) == 0, "write register 5");
	r.value = 0;
	CHECK(ioctl(fd, LFDREGS_READ, &r) == 0 && r.value == 0x1234, "read register 5 back");

	r.index = LFDREGS_NREGS;
	CHECK_ERR(ioctl(fd, LFDREGS_READ, &r), EINVAL, "index == NREGS is EINVAL");
	r.index = 0xffffffff;
	CHECK_ERR(ioctl(fd, LFDREGS_WRITE, &r), EINVAL, "huge index is EINVAL");

	memset(vals, 0xff, sizeof(vals));
	b.first = 4;
	b.count = 3;
	SET_BUF(b, vals);
	CHECK(ioctl(fd, LFDREGS_BULK_READ, &b) == 0, "BULK_READ registers 4..6");
	CHECK(vals[0] == 0 && vals[1] == 0x1234 && vals[2] == 0 && vals[3] == 0xffffffff,
	      "bulk data correct, nothing written past count");
	b.first = 63;
	b.count = 2;
	CHECK_ERR(ioctl(fd, LFDREGS_BULK_READ, &b), EINVAL, "range past the end is EINVAL");
	b.first = 1;
	b.count = 0xffffffff;
	CHECK_ERR(ioctl(fd, LFDREGS_BULK_READ, &b), EINVAL, "first + count overflow is EINVAL");
	b.first = 0;
	b.count = 2;
	SET_BUF(b, LFD_BAD_PTR);
	CHECK_ERR(ioctl(fd, LFDREGS_BULK_READ, &b), EFAULT, "bad buffer pointer is EFAULT");
	CHECK_ERR(ioctl(fd, LFDREGS_GET_INFO, LFD_BAD_PTR), EFAULT, "bad argument pointer is EFAULT");

	CHECK_ERR(ioctl(fd, _IO(LFDREGS_IOC_MAGIC, 0x7f)), ENOTTY, "unknown command is ENOTTY");
	CHECK(!isatty(fd) && errno == ENOTTY, "isatty() is false (TCGETS gives ENOTTY)");

	pid = fork();
	if (pid == 0) {
		if (setuid(65534))
			_exit(2);
		_exit(ioctl(fd, LFDREGS_RESET) == -1 && errno == EPERM ? 0 : 1);
	}
	waitpid(pid, &status, 0);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
	      "RESET by an unprivileged user is EPERM");
	CHECK(ioctl(fd, LFDREGS_RESET) == 0, "RESET as root");
	r.index = 5;
	CHECK(ioctl(fd, LFDREGS_READ, &r) == 0 && r.value == 0, "RESET cleared register 5");
	close(fd);
	return lfd_summary();
}
