// SPDX-License-Identifier: GPL-2.0
/*
 * input_test - wait for mouse events on an evdev node (Lab 22).
 *   input_test /dev/input/eventN [timeout_s]
 * Passes when it has seen REL_X > 0, REL_Y > 0 and a BTN_LEFT press.
 */
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include "lfd_test.h"

int main(int argc, char **argv)
{
	int fd, tmo = argc > 2 ? atoi(argv[2]) : 60, x = 0, y = 0, click = 0;
	char name[128] = "";
	struct input_event ev;
	time_t end;

	if (argc < 2)
		return 2;
	fd = open(argv[1], O_RDONLY);
	CHECK(fd >= 0, "open %s", argv[1]);
	if (fd < 0)
		return lfd_summary();
	ioctl(fd, EVIOCGNAME(sizeof(name)), name);
	CHECK(strstr(name, "lfd_mouse") != NULL, "EVIOCGNAME: \"%s\"", name);
	end = time(NULL) + tmo;
	while (time(NULL) < end && !(x && y && click)) {
		struct pollfd p = { .fd = fd, .events = POLLIN };

		if (poll(&p, 1, 1000) <= 0)
			continue;
		if (read(fd, &ev, sizeof(ev)) != sizeof(ev))
			continue;
		if (ev.type == EV_REL && ev.code == REL_X && ev.value > 0)
			x++;
		if (ev.type == EV_REL && ev.code == REL_Y && ev.value > 0)
			y++;
		if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 1)
			click++;
	}
	CHECK(x > 0, "REL_X events with dx > 0 (%d)", x);
	CHECK(y > 0, "REL_Y events with dy > 0 (%d)", y);
	CHECK(click > 0, "BTN_LEFT press events (%d)", click);
	close(fd);
	return lfd_summary();
}
