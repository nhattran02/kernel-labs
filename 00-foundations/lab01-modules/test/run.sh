#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 01 acceptance tests.  Run in the guest:  sh test/run.sh
# (or on the host: make qemu-test)
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
S=/sys/kernel/debug/lfd430/sensors
rmmod lfd_dummy 2>/dev/null; rmmod lfd_core 2>/dev/null
dmesg_reset

check_grep "lfd_dummy imports namespace LFD430 (.modinfo)" "^import_ns=LFD430$" strings ./lfd_dummy.ko
check_fail "lfd_dummy cannot load before lfd_core (unknown symbols)" insmod ./lfd_dummy.ko
check      "lfd_core loads with max_sensors=4" insmod ./lfd_core.ko max_sensors=4
check_grep "max_sensors visible in sysfs" "^4$" cat /sys/module/lfd_core/parameters/max_sensors
check_grep "max_sensors is read-only (0444)" "^-r--r--r--" ls -l /sys/module/lfd_core/parameters/max_sensors
check_grep "verbose is root-writable (0644)" "^-rw-r--r--" ls -l /sys/module/lfd_core/parameters/verbose
check      "lfd_dummy loads with names=a,b,c base=5" insmod ./lfd_dummy.ko names=a,b,c base=5
check_grep "sensor a reads 5"  "^a +5$"  cat $S
check_grep "sensor c reads 25" "^c +25$" cat $S
check      "base is writable at run time" sh -c "echo 100 > /sys/module/lfd_dummy/parameters/base"
check_grep "sensor b now reads 110" "^b +110$" cat $S
check_grep "lfd_core is in use by lfd_dummy" "^lfd_core .* lfd_dummy" lsmod
check_fail "rmmod lfd_core is refused while lfd_dummy uses it" unload lfd_core
check      "verbose=1 logs reads" sh -c "echo 1 > /sys/module/lfd_core/parameters/verbose && cat $S >/dev/null"
check_grep "a verbose read was logged" "lfd_core: a = 100" dmesg_since
check      "rmmod lfd_dummy" rmmod lfd_dummy
check_fail "five sensors exceed max_sensors=4" insmod ./lfd_dummy.ko names=a,b,c,d,e
check      "failed init rolled back every registration" test -z "$(cat $S)"
check_fail "duplicate names are rejected" insmod ./lfd_dummy.ko names=x,x
check      "failed init rolled back again" test -z "$(cat $S)"
check_fail "an empty name list is rejected" insmod ./lfd_dummy.ko names=
check      "rmmod lfd_core" rmmod lfd_core
check      "debugfs directory removed on unload" test ! -e /sys/kernel/debug/lfd430
check_clean_log
summary
