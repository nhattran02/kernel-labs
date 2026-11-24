// SPDX-License-Identifier: GPL-2.0
/*
 * lfdstat_stress - hammer /dev/lfdstat from many threads (Lab 07).
 *   lfdstat_stress [writers [events-per-writer]]
 *
 * Writer threads write values 1..1000 (cyclically); one thread flips the
 * configuration between two tags and limits; one thread takes snapshots.
 * At the end the statistics must account for every event.
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "uapi/lfdstat.h"
#include "lfd_test.h"

static int fd, nwriters = 4;
static long nevents = 20000;
static volatile int stop;
static long bad_snapshots, cfg_flips, snapshots;

static void *writer(void *arg)
{
	for (long i = 0; i < nevents; i++) {
		uint32_t v = i % 1000 + 1;

		if (write(fd, &v, sizeof(v)) != sizeof(v))
			break;
	}
	return NULL;
}

static void *cfg_flipper(void *arg)
{
	struct lfdstat_cfg_arg a = { 0 };

	while (!stop) {
		a.max_records = (cfg_flips & 1) ? 8 : 64;
		strcpy(a.tag, (cfg_flips & 1) ? "tag-A" : "tag-B");
		if (ioctl(fd, LFDSTAT_SET_CFG, &a) == 0)
			cfg_flips++;
	}
	return NULL;
}

static void *snapshotter(void *arg)
{
	struct lfdstat_snapshot s;

	while (!stop) {
		if (ioctl(fd, LFDSTAT_SNAPSHOT, &s))
			continue;
		snapshots++;
		if (!memchr(s.tag, 0, sizeof(s.tag)) ||
		    (strcmp(s.tag, "tag-A") && strcmp(s.tag, "tag-B") &&
		     strcmp(s.tag, "default")))
			bad_snapshots++;
	}
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t w[16], c, s;
	struct lfdstat_stats st;
	struct lfdstat_cfg_arg a = { 0 };
	uint64_t expect_sum = 0;
	int i;

	if (argc > 1)
		nwriters = atoi(argv[1]);
	if (argc > 2)
		nevents = atol(argv[2]);
	if (nwriters < 1 || nwriters > 16)
		nwriters = 4;
	fd = open("/dev/lfdstat", O_RDWR);
	CHECK(fd >= 0, "open /dev/lfdstat");
	if (fd < 0)
		return lfd_summary();
	CHECK(ioctl(fd, LFDSTAT_RESET) == 0, "RESET");

	pthread_create(&c, NULL, cfg_flipper, NULL);
	pthread_create(&s, NULL, snapshotter, NULL);
	for (i = 0; i < nwriters; i++)
		pthread_create(&w[i], NULL, writer, NULL);
	for (i = 0; i < nwriters; i++)
		pthread_join(w[i], NULL);
	stop = 1;
	pthread_join(c, NULL);
	pthread_join(s, NULL);

	for (long k = 0; k < nevents; k++)
		expect_sum += k % 1000 + 1;
	expect_sum *= nwriters;

	CHECK(ioctl(fd, LFDSTAT_GET_STATS, &st) == 0, "GET_STATS");
	DIAG("%d writers x %ld events, %ld config flips, %ld snapshots",
	     nwriters, nevents, cfg_flips, snapshots);
	CHECK(st.count == (uint64_t)nwriters * nevents, "no lost events: count %llu, expected %llu",
	      (unsigned long long)st.count, (unsigned long long)nwriters * nevents);
	CHECK(st.sum == expect_sum, "sum %llu, expected %llu",
	      (unsigned long long)st.sum, (unsigned long long)expect_sum);
	CHECK(st.min == 1 && st.max == 1000, "min/max are 1/1000 (got %u/%u)", st.min, st.max);
	CHECK(bad_snapshots == 0, "every snapshot had a valid tag (%ld bad)", bad_snapshots);
	CHECK(cfg_flips > 0 && snapshots > 0, "config flipper and snapshotter both ran");

	a.max_records = 4;
	strcpy(a.tag, "small");
	CHECK(ioctl(fd, LFDSTAT_SET_CFG, &a) == 0, "shrink max_records to 4");
	usleep(200000);
	ioctl(fd, LFDSTAT_GET_STATS, &st);
	CHECK(st.nrecords <= 4, "the ager trimmed the records to the new limit (%u)", st.nrecords);
	a.max_records = 0;
	CHECK_ERR(ioctl(fd, LFDSTAT_SET_CFG, &a), EINVAL, "max_records 0 is EINVAL");
	a.max_records = 8;
	memset(a.tag, 'x', sizeof(a.tag));
	CHECK_ERR(ioctl(fd, LFDSTAT_SET_CFG, &a), EINVAL, "unterminated tag is EINVAL");
	CHECK(ioctl(fd, LFDSTAT_RESET) == 0, "RESET");
	ioctl(fd, LFDSTAT_GET_STATS, &st);
	CHECK(st.count == 0 && st.nrecords == 0, "RESET clears everything");
	close(fd);
	return lfd_summary();
}
