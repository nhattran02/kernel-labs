#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 16 acceptance tests - run ON THE BEAGLEBONE BLACK (BusyBox or a
# regular distribution shell), booted with am335x-boneblack-lfd16.dtb and
# with the P8_12 - P8_14 jumper in place.
LAB=${LAB:-/root/lfd430}
. $LAB/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
D=/sys/kernel/debug/lfd_gpio_loop
mount | grep -q debugfs || mount -t debugfs debugfs /sys/kernel/debug
check "the overlay node is in the live DT" test -d /proc/device-tree/lfd-gpio-loop
check_grep "compatible" "lfd430,gpio-loopback" cat /proc/device-tree/lfd-gpio-loop/compatible
rmmod lfd_gpio_loop 2>/dev/null
dmesg_reset
check "lfd_gpio_loop loads" insmod ./lfd_gpio_loop.ko
check_grep "probe passed the wire self-test" "loopback OK, IRQ" dmesg_since
check_grep "IRQ registered on the GPIO irqchip" "lfd-gpio-loop" cat /proc/interrupts
check "1000 hard-IRQ measurements" sh -c "echo 1000 > $D/run"
cat $D/results
check_grep "no missed and no spurious edges" "n=1000 missed=0 spurious=0" cat $D/results
check "unload" unload lfd_gpio_loop
check "lfd_gpio_loop loads in threaded mode" insmod ./lfd_gpio_loop.ko threaded=1
check "1000 threaded measurements" sh -c "echo 1000 > $D/run"
cat $D/results
check_grep "threaded: no missed edges" "mode=threaded n=1000 missed=0" cat $D/results
check "unload" unload lfd_gpio_loop
check_clean_log all
summary
