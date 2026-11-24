// SPDX-License-Identifier: GPL-2.0
/* rtc_test - exercise /dev/rtc0 through the RTC character device (Lab 15). */
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "lfd_test.h"

static time_t tm_to_t(struct rtc_time *r)
{
	struct tm t = {
		.tm_sec = r->tm_sec, .tm_min = r->tm_min, .tm_hour = r->tm_hour,
		.tm_mday = r->tm_mday, .tm_mon = r->tm_mon, .tm_year = r->tm_year,
	};
	return timegm(&t);
}

static void t_to_tm(time_t s, struct rtc_time *r)
{
	struct tm t;

	gmtime_r(&s, &t);
	memset(r, 0, sizeof(*r));
	r->tm_sec = t.tm_sec;
	r->tm_min = t.tm_min;
	r->tm_hour = t.tm_hour;
	r->tm_mday = t.tm_mday;
	r->tm_mon = t.tm_mon;
	r->tm_year = t.tm_year;
}

int main(void)
{
	struct rtc_time rt;
	unsigned long data;
	time_t t0, start;
	int fd, n;

	fd = open("/dev/rtc0", O_RDONLY);
	CHECK(fd >= 0, "open /dev/rtc0");
	if (fd < 0)
		return lfd_summary();
	CHECK(ioctl(fd, RTC_RD_TIME, &rt) == 0, "RTC_RD_TIME");
	t0 = tm_to_t(&rt);
	DIAG("RTC reads %04d-%02d-%02d %02d:%02d:%02d UTC", rt.tm_year + 1900,
	     rt.tm_mon + 1, rt.tm_mday, rt.tm_hour, rt.tm_min, rt.tm_sec);
	CHECK(t0 > 1700000000, "the RTC was initialised by QEMU from the host clock");

	t_to_tm(1893456000, &rt);			/* 2030-01-01 00:00:00 */
	CHECK(ioctl(fd, RTC_SET_TIME, &rt) == 0, "RTC_SET_TIME 2030-01-01");
	ioctl(fd, RTC_RD_TIME, &rt);
	CHECK(tm_to_t(&rt) - 1893456000 <= 2, "reads back 2030-01-01 (+%ld s)",
	      (long)(tm_to_t(&rt) - 1893456000));
	t_to_tm(4354819200LL, &rt);			/* 2108-01-01: > U32_MAX */
	CHECK_ERR(ioctl(fd, RTC_SET_TIME, &rt), ERANGE,
		  "a time beyond the 32-bit counter is rejected (ERANGE)");

	ioctl(fd, RTC_RD_TIME, &rt);
	t_to_tm(tm_to_t(&rt) + 2, &rt);
	CHECK(ioctl(fd, RTC_ALM_SET, &rt) == 0, "alarm set 2 s ahead");
	CHECK(ioctl(fd, RTC_AIE_ON, 0) == 0, "alarm interrupt enabled");
	start = time(NULL);
	n = read(fd, &data, sizeof(data));
	CHECK(n == sizeof(data) && (data & RTC_AF), "read() returned an alarm event (data %#lx)", data);
	CHECK(time(NULL) - start >= 1 && time(NULL) - start <= 4,
	      "it took about 2 s (%ld s)", (long)(time(NULL) - start));
	CHECK(ioctl(fd, RTC_AIE_OFF, 0) == 0, "alarm interrupt disabled");
	close(fd);
	return lfd_summary();
}
