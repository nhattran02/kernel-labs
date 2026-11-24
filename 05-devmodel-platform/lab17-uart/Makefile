# SPDX-License-Identifier: GPL-2.0
# Part 1 runs in QEMU x86_64 on COM2 (make qemu-test).  Part 2 runs on the
# BeagleBone Black: make TARGET=bbb && make TARGET=bbb install.
USER_PROGS := test/tty_test
# pnpacpi=off: otherwise ACPI enumerates COM2 first, the 8250 driver takes
# it as ttyS0 and the console moves to COM2 (ttyS names follow discovery
# order, not I/O addresses).
QEMU_OPTS = --com2 null --append pnpacpi=off
include ../../common/Makefile.module

ifeq ($(TARGET),bbb)
DTS_INC := -I $(LFD_KSRC)/include -I $(LFD_KSRC)/arch/arm/boot/dts
BASE_DTB := $(LFD_KBUILD)/bbb/arch/arm/boot/dts/ti/omap/am335x-boneblack.dtb
all: am335x-boneblack-lfd17.dtb
lfd-uart1.dtbo: dts/lfd-uart1.dtso
	cpp -nostdinc $(DTS_INC) -undef -x assembler-with-cpp $< | \
		dtc -@ -I dts -O dtb -o $@ -
am335x-boneblack-lfd17.dtb: lfd-uart1.dtbo $(BASE_DTB)
	fdtoverlay -i $(BASE_DTB) -o $@ $<
endif
clean: clean-dtb
clean-dtb:
	rm -f lfd-uart1.dtbo am335x-boneblack-lfd17.dtb
.PHONY: clean-dtb
