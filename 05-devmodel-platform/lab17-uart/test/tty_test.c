// SPDX-License-Identifier: GPL-2.0
/*
 * tty_test - exercise /dev/ttyLFD0 in internal loopback (Lab 17).
 *   tty_test [device]
 */
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/serial.h>
#include "lfd_test.h"

/* From <asm-generic/termios.h>; glibc's headers do not export it. */
#ifndef TIOCM_LOOP
#define TIOCM_LOOP 0x8000
#endif

static int set_speed(int fd, speed_t sp)
{
	struct termios t;

	if (tcgetattr(fd, &t))
		return -1;
	cfmakeraw(&t);
	t.c_cflag |= CLOCAL | CREAD;
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	cfsetispeed(&t, sp);
	cfsetospeed(&t, sp);
	return tcsetattr(fd, TCSANOW, &t);
}

/* Write n bytes and read them back through the loopback. */
static int echo(int fd, int n, int timeout_ms)
{
	unsigned char *tx = malloc(n), *rx = malloc(n);
	int got = 0, sent = 0, i, ok;

	for (i = 0; i < n; i++)
		tx[i] = (unsigned char)(i * 13 + 7);
	tcflush(fd, TCIOFLUSH);
	while (got < n) {
		struct pollfd p = { .fd = fd, .events = POLLIN | (sent < n ? POLLOUT : 0) };

		if (poll(&p, 1, timeout_ms) <= 0)
			break;
		if ((p.revents & POLLOUT) && sent < n) {
			int w = write(fd, tx + sent, n - sent);

			if (w > 0)
				sent += w;
		}
		if (p.revents & POLLIN) {
			int r = read(fd, rx + got, n - got);

			if (r > 0)
				got += r;
		}
	}
	ok = got == n && !memcmp(tx, rx, n);
	if (!ok)
		DIAG("sent %d, received %d", sent, got);
	free(tx);
	free(rx);
	return ok;
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/ttyLFD0";
	struct serial_icounter_struct ic0, ic1;
	struct termios t;
	int fd, bits = TIOCM_LOOP, m = 0;

	fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
	CHECK(fd >= 0, "open %s", dev);
	if (fd < 0)
		return lfd_summary();
	CHECK(set_speed(fd, B115200) == 0, "raw mode, 115200 baud");
	tcgetattr(fd, &t);
	CHECK(cfgetospeed(&t) == B115200, "speed reads back as 115200");
	CHECK(ioctl(fd, TIOCMBIS, &bits) == 0, "enable internal loopback (TIOCM_LOOP)");
	ioctl(fd, TIOCMGET, &m);
	CHECK(m & TIOCM_CTS, "modem status: CTS reported");
	CHECK(ioctl(fd, TIOCGICOUNT, &ic0) == 0, "TIOCGICOUNT");
	CHECK(echo(fd, 4096, 3000), "4096 bytes echoed intact at 115200");
	ioctl(fd, TIOCGICOUNT, &ic1);
	CHECK(ic1.tx - ic0.tx == 4096 && ic1.rx - ic0.rx == 4096,
	      "icount: tx +%d rx +%d", ic1.tx - ic0.tx, ic1.rx - ic0.rx);
	CHECK(ic1.overrun == ic0.overrun, "no overruns");
	CHECK(set_speed(fd, B9600) == 0, "switch to 9600 baud");
	CHECK(echo(fd, 256, 3000), "256 bytes echoed intact at 9600");
	CHECK(tcdrain(fd) == 0, "tcdrain() returns (tx_empty works)");
	ioctl(fd, TIOCMBIC, &bits);
	close(fd);
	return lfd_summary();
}
