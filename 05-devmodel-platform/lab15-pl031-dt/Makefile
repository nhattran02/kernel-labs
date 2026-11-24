# SPDX-License-Identifier: GPL-2.0
# Lab 15 runs on QEMU arm64 "virt": build the arm64 lab kernel first
# (env/scripts/build-kernel.sh arm64) and an arm64 initramfs.
TARGET ?= arm64
USER_PROGS := test/rtc_test
QEMU_OPTS = --dtb $(CURDIR)/virt-lfd.dtb
include ../../common/Makefile.module

# The board DT: QEMU's generated DT with our overlay applied.
all: virt-lfd.dtb
virt-lfd.dtb: dts/lfd-pl031.dtso dts/mk-dtb.sh
	sh dts/mk-dtb.sh $(LFD_MEM) $@
clean: clean-dtb
clean-dtb:
	rm -f virt-lfd.dtb virt.dtb virt.dts lfd-pl031.dtbo
.PHONY: clean-dtb
