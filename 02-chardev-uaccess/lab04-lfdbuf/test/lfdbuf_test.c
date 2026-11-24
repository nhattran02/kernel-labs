// SPDX-License-Identifier: GPL-2.0
/*
 * lfdbuf_test - user-space acceptance test for Lab 04.
 * Usage: lfdbuf_test [/dev/lfdbuf0 /dev/lfdbuf1]
 */
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include "lfd_test.h"

static const char *dev0 = "/dev/lfdbuf0", *dev1 = "/dev/lfdbuf1";

static long sysfs_long(const char *dev, const char *attr)
{
	char path[128], buf[32] = "";
	int fd;

	snprintf(path, sizeof(path), "/sys/class/lfdbuf/%s/%s",
		 strrchr(dev, '/') + 1, attr);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	if (read(fd, buf, sizeof(buf) - 1) < 0)
		buf[0] = '\0';
	close(fd);
	return strtol(buf, NULL, 10);
}

static int sysfs_write(const char *dev, const char *attr, const char *val)
{
	char path[128];
	int fd, ret;

	snprintf(path, sizeof(path), "/sys/class/lfdbuf/%s/%s",
		 strrchr(dev, '/') + 1, attr);
	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	ret = write(fd, val, strlen(val));
	close(fd);
	return ret;
}

int main(int argc, char **argv)
{
	char buf[256], zero[8] = { 0 };
	long size, opens;
	int fd, fd1;
	ssize_t n;

	if (argc == 3) {
		dev0 = argv[1];
		dev1 = argv[2];
	}
	size = sysfs_long(dev0, "size");
	CHECK(size >= 64, "size attribute readable (%ld)", size);
	opens = sysfs_long(dev0, "opens");

	/* --- basic write / read back ----------------------------------- */
	fd = open(dev0, O_RDWR | O_TRUNC);
	CHECK(fd >= 0, "open %s O_RDWR|O_TRUNC", dev0);
	if (fd < 0)
		return lfd_summary();
	CHECK(sysfs_long(dev0, "opens") == opens + 1, "opens counter incremented");
	CHECK(write(fd, "hello", 5) == 5, "write 5 bytes");
	CHECK(lseek(fd, 0, SEEK_CUR) == 5, "file position advanced to 5");
	CHECK(lseek(fd, 0, SEEK_SET) == 0, "lseek(0, SEEK_SET)");
	n = read(fd, buf, sizeof(buf));
	CHECK(n == 5 && !memcmp(buf, "hello", 5), "read returns exactly the 5 bytes written");
	CHECK(read(fd, buf, sizeof(buf)) == 0, "read at end of data returns 0 (EOF)");
	CHECK(sysfs_long(dev0, "len") == 5, "len attribute is 5");

	/* --- O_TRUNC and gaps --------------------------------------------- */
	close(fd);
	fd = open(dev0, O_RDWR | O_TRUNC);
	CHECK(sysfs_long(dev0, "len") == 0, "O_TRUNC empties the device");
	CHECK(pwrite(fd, "X", 1, 10) == 1, "pwrite 1 byte at offset 10");
	CHECK(sysfs_long(dev0, "len") == 11, "len grows to 11");
	n = pread(fd, buf, sizeof(buf), 0);
	CHECK(n == 11, "pread returns 11 bytes");
	CHECK(!memcmp(buf, zero, 5) && !memcmp(buf + 5, zero, 5) && buf[10] == 'X',
	      "the gap reads as zeros, no stale \"hello\"");

	/* --- lseek ----------------------------------------------------------- */
	CHECK(lseek(fd, 0, SEEK_END) == 11, "SEEK_END is relative to the data (11)");
	CHECK(lseek(fd, -1, SEEK_END) == 10, "SEEK_END -1");
	CHECK(lseek(fd, size, SEEK_SET) == size, "seek to the capacity is allowed");
	CHECK_ERR(lseek(fd, size + 1, SEEK_SET), EINVAL, "seek past the capacity is EINVAL");
	CHECK_ERR(lseek(fd, -1, SEEK_SET), EINVAL, "negative position is EINVAL");

	/* --- capacity ---------------------------------------------------------- */
	lseek(fd, size, SEEK_SET);
	CHECK_ERR(write(fd, "a", 1), ENOSPC, "write at the capacity is ENOSPC");
	lseek(fd, size - 2, SEEK_SET);
	CHECK(write(fd, "abcd", 4) == 2, "write straddling the capacity is short (2)");
	CHECK(sysfs_long(dev0, "len") == size, "len reaches the capacity");

	/* --- bad user pointers --------------------------------------------- */
	lseek(fd, 0, SEEK_SET);
	CHECK_ERR(read(fd, LFD_BAD_PTR, 4), EFAULT, "read into a bad pointer is EFAULT");
	CHECK_ERR(write(fd, LFD_BAD_PTR, 4), EFAULT, "write from a bad pointer is EFAULT");

	/* --- devices are independent ------------------------------------------ */
	fd1 = open(dev1, O_RDWR | O_TRUNC);
	CHECK(fd1 >= 0, "open %s", dev1);
	CHECK(write(fd1, "other", 5) == 5, "write to the second device");
	CHECK(sysfs_long(dev0, "len") == size, "first device unchanged");
	close(fd1);

	/* --- sysfs clear --------------------------------------------------------- */
	CHECK(sysfs_write(dev0, "clear", "0") < 0 && errno == EINVAL, "clear rejects \"0\"");
	CHECK(sysfs_write(dev0, "clear", "1") == 1, "clear accepts \"1\"");
	CHECK(sysfs_long(dev0, "len") == 0, "clear empties the device");
	lseek(fd, 0, SEEK_SET);
	CHECK(read(fd, buf, sizeof(buf)) == 0, "read after clear returns EOF");
	close(fd);
	return lfd_summary();
}
