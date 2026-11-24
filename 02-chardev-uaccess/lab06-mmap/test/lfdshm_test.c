// SPDX-License-Identifier: GPL-2.0
/*
 * lfdshm_test - acceptance test for Lab 06.
 *   lfdshm_test remap|fault    run the checks for the module's mode
 *   lfdshm_test hold SECONDS   keep the buffer mapped (lifetime test)
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "uapi/lfdshm.h"
#include "lfd_test.h"

static long pg;

static uint64_t pagemap_pfn(void *addr, int *present)
{
	uint64_t e = 0;
	int fd = open("/proc/self/pagemap", O_RDONLY);

	if (fd < 0 || pread(fd, &e, 8, ((uintptr_t)addr / pg) * 8) != 8)
		e = 0;
	if (fd >= 0)
		close(fd);
	*present = !!(e >> 63);
	return e & ((1ULL << 55) - 1);
}

static uint64_t faults(int fd)
{
	struct lfdshm_info i = { 0 };

	ioctl(fd, LFDSHM_GET_INFO, &i);
	return i.faults;
}

int main(int argc, char **argv)
{
	struct lfdshm_info info = { 0 };
	struct lfdshm_fill fl;
	uint64_t pfns[4];
	char tmp[16];
	char *p, *p2, *q;
	int fd, fault_mode, ok, present, i;

	pg = sysconf(_SC_PAGESIZE);
	fd = open("/dev/lfdshm", O_RDWR);
	if (argc == 3 && !strcmp(argv[1], "hold")) {
		p = mmap(NULL, pg, PROT_READ, MAP_SHARED, fd, 0);
		if (p == MAP_FAILED)
			return 1;
		close(fd);		/* the mapping alone keeps the file open */
		sleep(atoi(argv[2]));
		return 0;
	}
	fault_mode = argc > 1 && !strcmp(argv[1], "fault");
	CHECK(fd >= 0, "open /dev/lfdshm");
	if (fd < 0)
		return lfd_summary();
	CHECK(ioctl(fd, LFDSHM_GET_INFO, &info) == 0 && info.size >= 16 * (uint64_t)pg,
	      "GET_INFO: %llu bytes", (unsigned long long)info.size);

	/* --- mmap ------------------------------------------------------------ */
	p = mmap(NULL, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	CHECK(p != MAP_FAILED, "mmap the whole buffer MAP_SHARED");
	if (p == MAP_FAILED)
		return lfd_summary();
	if (fault_mode) {
		uint64_t f0 = faults(fd);

		/* volatile: the compiler must not drop these "useless" loads */
		*(volatile char *)p = 1;
		(void)*(volatile char *)(p + 5 * pg);
		(void)*(volatile char *)(p + 9 * pg);
		CHECK(faults(fd) - f0 == 3, "fault mode: touching 3 pages costs 3 faults (%llu)",
		      (unsigned long long)(faults(fd) - f0));
		(void)*(volatile char *)(p + 5 * pg);
		CHECK(faults(fd) - f0 == 3, "fault mode: touching a mapped page again is free");
	} else {
		CHECK(faults(fd) == 0, "remap mode: no faults counted");
	}
	memcpy(p, "via-mmap", 9);
	CHECK(pread(fd, tmp, 9, 0) == 9 && !strcmp(tmp, "via-mmap"),
	      "a store through the mapping is visible to read()");
	CHECK(pwrite(fd, "via-write", 10, pg) == 10, "pwrite at page 1");
	CHECK(!strcmp(p + pg, "via-write"), "a write() is visible through the mapping");

	p2 = mmap(NULL, info.size - pg, PROT_READ, MAP_SHARED, fd, pg);
	CHECK(p2 != MAP_FAILED && !strcmp(p2, "via-write"),
	      "mapping at offset 1 page starts at page 1");
	if (p2 != MAP_FAILED)
		munmap(p2, info.size - pg);
	munmap(p, info.size);

	CHECK(mmap(NULL, info.size + pg, PROT_READ, MAP_SHARED, fd, 0) == MAP_FAILED &&
	      errno == EINVAL, "mapping larger than the buffer is EINVAL");
	CHECK(mmap(NULL, pg, PROT_READ, MAP_SHARED, fd, info.size) == MAP_FAILED &&
	      errno == EINVAL, "offset at the end of the buffer is EINVAL");
	CHECK(mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0) == MAP_FAILED &&
	      errno == EINVAL, "MAP_PRIVATE is EINVAL");

	/* --- pinned user pages ------------------------------------------------ */
	q = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	memset(&fl, 0, sizeof(fl));
	fl.addr = (uintptr_t)q;
	fl.len = 4 * pg;
	fl.pfns = (uintptr_t)pfns;
	fl.pattern = 0xa5;
	CHECK(ioctl(fd, LFDSHM_FILL_USER, &fl) == 0, "FILL_USER on 4 fresh anonymous pages");
	for (ok = 1, i = 0; i < 4 * pg; i++)
		ok &= (unsigned char)q[i] == 0xa5;
	CHECK(ok, "the kernel's pattern is in every byte of the user buffer");
	for (ok = 1, i = 0; i < 4; i++) {
		uint64_t pfn = pagemap_pfn(q + i * pg, &present);

		ok &= present && pfn == pfns[i];
		if (!(present && pfn == pfns[i]))
			DIAG("page %d: kernel pfn %#llx, pagemap %#llx (present %d)", i,
			     (unsigned long long)pfns[i], (unsigned long long)pfn, present);
	}
	CHECK(ok, "pinned PFNs match /proc/self/pagemap");

	fl.addr = (uintptr_t)q + 1;
	CHECK_ERR(ioctl(fd, LFDSHM_FILL_USER, &fl), EINVAL, "unaligned address is EINVAL");
	fl.addr = (uintptr_t)q;
	fl.len = 0;
	CHECK_ERR(ioctl(fd, LFDSHM_FILL_USER, &fl), EINVAL, "zero length is EINVAL");
	fl.len = 65 * pg;
	CHECK_ERR(ioctl(fd, LFDSHM_FILL_USER, &fl), EINVAL, "more than 64 pages is EINVAL");
	fl.len = pg;
	fl.pad = 1;
	CHECK_ERR(ioctl(fd, LFDSHM_FILL_USER, &fl), EINVAL, "non-zero pad is EINVAL");
	fl.pad = 0;
	mprotect(q, 4 * pg, PROT_READ);
	CHECK_ERR(ioctl(fd, LFDSHM_FILL_USER, &fl), EFAULT, "read-only user pages cannot be pinned for writing");
	munmap(q, 4 * pg);
	CHECK_ERR(ioctl(fd, LFDSHM_FILL_USER, &fl), EFAULT, "unmapped user memory is EFAULT");
	close(fd);
	return lfd_summary();
}
