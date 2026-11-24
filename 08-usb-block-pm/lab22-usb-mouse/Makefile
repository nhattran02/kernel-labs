# SPDX-License-Identifier: GPL-2.0
# QEMU: an xHCI controller with QEMU's USB mouse.  The monitor commands sent
# by --hmp move the mouse and click while the test waits for events.
USER_PROGS := test/input_test
QEMU_OPTS = --usb-mouse --hmp "sleep 15;$(MOVES)"
MOVES := $(shell for i in $$(seq 1 60); do printf 'mouse_move 7 3;mouse_button 1;mouse_button 0;sleep 1;'; done)
include ../../common/Makefile.module
