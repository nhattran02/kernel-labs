// SPDX-License-Identifier: GPL-2.0
/* lfdpipe_test - acceptance test for Lab 08 (expects the default size 4096). */
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include "lfd_test.h"

#define DEV "/dev/lfdpipe"
#define CAP 4096

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static short revents(int fd)
{
	struct pollfd p = { .fd = fd, .events = POLLIN | POLLOUT };

	return poll(&p, 1, 0) == 1 ? p.revents : 0;
}

struct delayed { int fd; int ms; const char *data; };

static void *delayed_write(void *arg)
{
	struct delayed *d = arg;

	usleep(d->ms * 1000);
	if (d->data)
		write(d->fd, d->data, strlen(d->data));
	else
		close(d->fd);
	return NULL;
}

static void *delayed_read(void *arg)
{
	struct delayed *d = arg;
	char buf[CAP];

	usleep(d->ms * 1000);
	read(d->fd, buf, 100);
	return NULL;
}

static void on_alarm(int sig) { }

/* Stream 1 MiB through the pipe and check every byte. */
#define STREAM (1 << 20)
static int sfd_w, sfd_r;
static void *producer(void *arg)
{
	static unsigned char b[1000];
	long sent = 0;

	while (sent < STREAM) {
		long n = sizeof(b) < (size_t)(STREAM - sent) ? (long)sizeof(b) : STREAM - sent;

		for (long i = 0; i < n; i++)
			b[i] = (unsigned char)((sent + i) * 7 + 3);
		n = write(sfd_w, b, n);
		if (n <= 0)
			break;
		sent += n;
	}
	close(sfd_w);
	return NULL;
}

int main(void)
{
	char buf[2 * CAP];
	struct delayed d;
	struct sigaction sa;
	struct epoll_event ev;
	pthread_t t;
	double t0;
	int w, r, ep, n, ok;
	long total, bad;

	w = open(DEV, O_WRONLY);
	r = open(DEV, O_RDONLY | O_NONBLOCK);
	CHECK(w >= 0 && r >= 0, "open a writer and a non-blocking reader");
	if (w < 0 || r < 0)
		return lfd_summary();

	CHECK_ERR(read(r, buf, 10), EAGAIN, "empty FIFO, writer present: O_NONBLOCK read is EAGAIN");
	CHECK((revents(r) & (POLLIN | POLLOUT)) == POLLOUT, "poll: empty FIFO is writable, not readable");
	CHECK(write(w, "0123456789", 10) == 10, "write 10 bytes");
	CHECK(revents(r) & POLLIN, "poll: readable after a write");
	CHECK(read(r, buf, sizeof(buf)) == 10 && !memcmp(buf, "0123456789", 10),
	      "read of 8 KiB returns the 10 bytes available");

	fcntl(w, F_SETFL, O_NONBLOCK);
	memset(buf, 'f', sizeof(buf));
	CHECK(write(w, buf, sizeof(buf)) == CAP, "a write larger than the space is short (%d)", CAP);
	CHECK_ERR(write(w, buf, 1), EAGAIN, "full FIFO: O_NONBLOCK write is EAGAIN");
	CHECK((revents(w) & (POLLIN | POLLOUT)) == POLLIN, "poll: full FIFO is readable, not writable");

	/* A blocked writer is woken when a reader makes room. */
	fcntl(w, F_SETFL, 0);
	d = (struct delayed){ r, 200, NULL };
	pthread_create(&t, NULL, delayed_read, &d);
	t0 = now();
	n = write(w, "x", 1);
	CHECK(n == 1 && now() - t0 > 0.15, "blocking write waited for space (%.2f s)", now() - t0);
	pthread_join(t, NULL);
	while (read(r, buf, sizeof(buf)) > 0)
		;

	/* A blocked reader is woken by a writer. */
	fcntl(r, F_SETFL, 0);
	d = (struct delayed){ w, 200, "late" };
	pthread_create(&t, NULL, delayed_write, &d);
	t0 = now();
	n = read(r, buf, sizeof(buf));
	CHECK(n == 4 && now() - t0 > 0.15, "blocking read waited for data (%.2f s)", now() - t0);
	pthread_join(t, NULL);

	/* Signals: without SA_RESTART a blocked read fails with EINTR. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	sigaction(SIGALRM, &sa, NULL);
	alarm(1);
	CHECK_ERR(read(r, buf, sizeof(buf)), EINTR, "a signal interrupts a blocked read (EINTR)");
	/* With SA_RESTART the kernel restarts it (-ERESTARTSYS at work). */
	sa.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa, NULL);
	alarm(1);
	d = (struct delayed){ w, 1500, "after-restart" };
	pthread_create(&t, NULL, delayed_write, &d);
	n = read(r, buf, sizeof(buf));
	CHECK(n == 13, "with SA_RESTART the read is restarted and gets the data");
	pthread_join(t, NULL);

	/* epoll */
	ep = epoll_create1(0);
	ev.events = EPOLLIN;
	ev.data.fd = r;
	epoll_ctl(ep, EPOLL_CTL_ADD, r, &ev);
	CHECK(epoll_wait(ep, &ev, 1, 0) == 0, "epoll: nothing to read yet");
	d = (struct delayed){ w, 100, "ep" };
	pthread_create(&t, NULL, delayed_write, &d);
	CHECK(epoll_wait(ep, &ev, 1, 2000) == 1 && (ev.events & EPOLLIN), "epoll wakes up on data");
	pthread_join(t, NULL);
	read(r, buf, sizeof(buf));
	close(ep);

	/* End of file: the last writer closes while a reader is blocked. */
	d = (struct delayed){ w, 200, NULL };
	pthread_create(&t, NULL, delayed_write, &d);
	t0 = now();
	n = read(r, buf, sizeof(buf));
	CHECK(n == 0 && now() - t0 > 0.15, "a blocked reader sees EOF when the last writer closes");
	pthread_join(t, NULL);
	CHECK(revents(r) & POLLHUP, "poll reports POLLHUP without writers");
	close(r);

	/* Data integrity under streaming. */
	sfd_w = open(DEV, O_WRONLY);
	sfd_r = open(DEV, O_RDONLY);
	pthread_create(&t, NULL, producer, NULL);
	total = bad = 0;
	while ((n = read(sfd_r, buf, 3000)) > 0) {
		for (int i = 0; i < n; i++)
			bad += (unsigned char)buf[i] != (unsigned char)((total + i) * 7 + 3);
		total += n;
	}
	pthread_join(t, NULL);
	ok = total == STREAM && !bad;
	CHECK(ok, "1 MiB streamed intact, then EOF (%ld bytes, %ld bad)", total, bad);
	close(sfd_r);
	return lfd_summary();
}
