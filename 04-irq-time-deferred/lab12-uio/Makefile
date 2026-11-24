# SPDX-License-Identifier: GPL-2.0
# Lab 12 has no kernel module: it drives the edu device from user space
# through the in-tree uio_pci_generic driver.
USER_PROGS := edu_uio
QEMU_OPTS = --edu
NO_MODULES := 1
include ../../common/Makefile.module
