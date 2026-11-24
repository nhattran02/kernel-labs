# SPDX-License-Identifier: GPL-2.0
# The test program is built twice: 64-bit, and 32-bit to exercise the compat
# path of the ioctl interface.  For the 32-bit build install the i686 cross
# compiler (Ubuntu: gcc-i686-linux-gnu); it coexists with the ARM cross
# compilers, whereas gcc-multilib (gcc -m32) conflicts with them.
USER_PROGS := test/lfdregs_test
EXTRA_PROGS := test/lfdregs_test32
CC32 ?= $(shell command -v i686-linux-gnu-gcc >/dev/null && echo i686-linux-gnu-gcc || echo "gcc -m32")
include ../../common/Makefile.module

test/lfdregs_test32: test/lfdregs_test.c include/uapi/lfdregs.h
	$(CC32) $(USER_CFLAGS) -o $@ $<
