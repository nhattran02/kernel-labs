// SPDX-License-Identifier: GPL-2.0
/*
 * edu_uio - a user-space driver for QEMU's edu device (LFD430 Lab 12).
 *
 * The device must be bound to uio_pci_generic (see test/run.sh).  Then:
 *   - BAR0 is mapped from /sys/bus/pci/devices/<BDF>/resource0;
 *   - interrupts are delivered through /dev/uioN: read() blocks until the
 *     next one and returns the total interrupt count;
 *   - uio_pci_generic's kernel handler masks INTx by setting the Interrupt
 *     Disable bit (bit 10) of the PCI command register; the user-space
 *     driver clears it again through /sys/bus/pci/devices/<BDF>/config.
 *     (uio_pci_generic has no irqcontrol: writing to /dev/uioN gives
 *     -ENOSYS.  See Documentation/driver-api/uio-howto.rst.)
 *
 *   edu_uio <BDF> raise <n>   raise n interrupts, report latency
 *   edu_uio <BDF> fact <n>    compute n! interrupt-driven
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include "edu_regs.h"

#define BAR_SIZE (1 << 20)

/* User space: volatile is right for an mmap()ed register window. */
static volatile uint32_t *regs;
static int uio_fd, cfg_fd;

static inline uint32_t rd(unsigned int off) { return regs[off / 4]; }
static inline void wr(unsigned int off, uint32_t v) { regs[off / 4] = v; }

static double now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

/* Find /dev/uioN for the PCI device <bdf>. */
static int open_uio(const char *bdf)
{
	char path[300];
	struct dirent *de;
	DIR *d;
	int fd = -1;

	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/uio", bdf);
	d = opendir(path);
	if (!d)
		return -1;
	while ((de = readdir(d))) {
		if (!strncmp(de->d_name, "uio", 3)) {
			snprintf(path, sizeof(path), "/dev/%s", de->d_name);
			fd = open(path, O_RDWR);
			break;
		}
	}
	closedir(d);
	return fd;
}

static int map_bar0(const char *bdf)
{
	char path[300];
	void *p;
	int fd;

	/*
	 * TODO T1: open /sys/bus/pci/devices/<bdf>/resource0 (O_RDWR | O_SYNC)
	 *   and mmap BAR_SIZE bytes of it, MAP_SHARED, read/write.  Store
	 *   the result in regs.  The fd can be closed after mmap().
	 *   Deck 05 slide 19.
	 */
	(void)path; (void)p; (void)fd;
	return -1;
}

/*
 * Unmask INTx: clear PCI_COMMAND_INTX_DISABLE (bit 10 of the command
 * register at config offset 4, i.e. bit 2 of the byte at offset 5).
 */
static int irq_enable(void)
{
	/*
	 * TODO T2: pread() the byte at offset 5 of cfg_fd, clear bit 0x04,
	 *   pwrite() it back.  Return 0 or -1.
	 */
	return -1;
}

/*
 * Wait up to timeout_ms for one interrupt and handle it.
 * Returns the device's IRQ_STATUS value, 0 on timeout, -1 on error.
 */
static long wait_irq(int timeout_ms)
{
	/*
	 * TODO T3: poll() uio_fd for POLLIN with the timeout; then read() the
	 *   32-bit interrupt count.  Handle the device: read IRQ_STATUS and
	 *   write it to IRQ_ACK.  Only then unmask with irq_enable(): what
	 *   would happen with the opposite order on a level-triggered line?
	 */
	return -1;
}

static int do_raise(int n)
{
	double t0, lat, min = 1e9, max = 0, sum = 0;
	int i;

	for (i = 0; i < n; i++) {
		t0 = now_us();
		wr(EDU_REG_IRQ_RAISE, 0x1000);
		if (wait_irq(1000) != 0x1000) {
			fprintf(stderr, "interrupt %d: missing or wrong status\n", i);
			return 1;
		}
		lat = now_us() - t0;
		sum += lat;
		if (lat < min)
			min = lat;
		if (lat > max)
			max = lat;
	}
	printf("raised %d handled %d lat_us min %.0f avg %.0f max %.0f\n",
	       n, i, min, sum / n, max);
	return 0;
}

static int do_fact(unsigned int n)
{
	long st;

	/*
	 * TODO T4: Same protocol as Lab 10, but from user space: set
	 *   EDU_STATUS_IRQFACT, write n to EDU_REG_FACT, wait for an
	 *   interrupt with EDU_IRQ_FACT set, print "fact %u = %u".
	 */
	(void)st; (void)n;
	return 1;
}

int main(int argc, char **argv)
{
	char path[300];
	uint32_t id;

	if (argc != 4) {
		fprintf(stderr, "usage: %s <BDF> raise <n> | fact <n>\n", argv[0]);
		return 2;
	}
	if (map_bar0(argv[1])) {
		perror("map BAR0");
		return 1;
	}
	id = rd(EDU_REG_ID);
	if ((id & 0xff) != 0xed) {
		fprintf(stderr, "not an edu device: %#x\n", id);
		return 1;
	}
	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/config", argv[1]);
	cfg_fd = open(path, O_RDWR);
	uio_fd = open_uio(argv[1]);
	if (uio_fd < 0 || cfg_fd < 0 || irq_enable()) {
		fprintf(stderr, "no /dev/uioN for %s (bound to uio_pci_generic?)\n", argv[1]);
		return 1;
	}
	printf("edu v%u.%u via UIO\n", id >> 24, (id >> 16) & 0xff);
	if (!strcmp(argv[2], "raise"))
		return do_raise(atoi(argv[3]));
	if (!strcmp(argv[2], "fact"))
		return do_fact(atoi(argv[3]));
	return 2;
}
