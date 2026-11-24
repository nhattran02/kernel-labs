/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lfd_test.h - tiny TAP-style test helpers for the lab test programs.
 *
 *   CHECK(cond, "what is being checked");
 *   CHECK_ERR(call, ENOTTY, "ioctl rejects an unknown command");
 *   return lfd_summary();
 *
 * Output lines start with "ok" or "not ok"; the exit status is the number
 * of failed checks (capped at 100), so test/run.sh can simply chain them.
 */
#ifndef LFD_TEST_H
#define LFD_TEST_H

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int lfd_n, lfd_failed;

/* An invalid user pointer the compiler cannot see through (EFAULT tests). */
static void *volatile lfd_bad_ptr = (void *)1;
#define LFD_BAD_PTR ((void *)lfd_bad_ptr)

#define CHECK(cond, ...) do {						\
	int __ok = !!(cond);						\
	lfd_n++;							\
	printf("%s %d - ", __ok ? "ok" : "not ok", lfd_n);		\
	printf(__VA_ARGS__);						\
	if (!__ok) {							\
		lfd_failed++;						\
		printf("   [%s:%d: %s]", __FILE__, __LINE__, #cond);	\
	}								\
	printf("\n");							\
	fflush(stdout);							\
} while (0)

/* Expect @call to return -1 with errno == @err. */
#define CHECK_ERR(call, err, ...) do {					\
	errno = 0;							\
	long __r = (long)(call);					\
	int __e = errno;						\
	CHECK(__r == -1 && __e == (err), __VA_ARGS__);			\
	if (!(__r == -1 && __e == (err)))				\
		printf("#   got ret=%ld errno=%d (%s), want errno=%d (%s)\n", \
		       __r, __e, strerror(__e), (err), strerror(err));	\
} while (0)

#define DIAG(...) do { printf("# "); printf(__VA_ARGS__); printf("\n"); } while (0)

static inline int lfd_summary(void)
{
	printf("# %d/%d checks passed\n", lfd_n - lfd_failed, lfd_n);
	return lfd_failed > 100 ? 100 : lfd_failed;
}

#endif /* LFD_TEST_H */
