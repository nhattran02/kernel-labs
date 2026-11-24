# SPDX-License-Identifier: GPL-2.0
# Lab 16 runs on the BeagleBone Black: make (TARGET=bbb is the default here),
# then "make install" copies the lab to $(LFD_BBB_ROOTFS)/root/lfd430/.
TARGET ?= bbb
include ../../common/Makefile.module

# Board DT with the overlay applied, for TFTP boot (or apply the .dtbo in
# U-Boot with "fdt apply").  Needs the BBB kernel tree built with DTC_FLAGS=-@.
DTS_INC := -I $(LFD_KSRC)/include -I $(LFD_KSRC)/arch/arm/boot/dts
BASE_DTB := $(LFD_KBUILD)/bbb/arch/arm/boot/dts/ti/omap/am335x-boneblack.dtb
all: lfd-gpio-loop.dtbo am335x-boneblack-lfd16.dtb
lfd-gpio-loop.dtbo: dts/lfd-gpio-loop.dtso
	cpp -nostdinc $(DTS_INC) -undef -x assembler-with-cpp $< | \
		dtc -@ -I dts -O dtb -o $@ -
am335x-boneblack-lfd16.dtb: lfd-gpio-loop.dtbo $(BASE_DTB)
	fdtoverlay -i $(BASE_DTB) -o $@ $<
clean: clean-dtb
clean-dtb:
	rm -f lfd-gpio-loop.dtbo am335x-boneblack-lfd16.dtb
.PHONY: clean-dtb
