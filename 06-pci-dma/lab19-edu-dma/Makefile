# SPDX-License-Identifier: GPL-2.0
USER_PROGS := test/dma_test
# 1 GiB of RAM with 64-256 MiB reserved (memmap=192M$64M): every normal
# allocation then lands above 256 MiB, out of reach of edu's 28-bit DMA,
# as on a big, long-running machine.  nokaslr keeps the kernel image at
# 16 MiB, below the hole.
QEMU_OPTS = --edu --mem 1024 --append 'nokaslr memmap=192M$$64M'
include ../../common/Makefile.module
