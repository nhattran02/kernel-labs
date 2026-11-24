#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 13 acceptance tests.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
B=/sys/bus/lfd
rmmod lfd_temp 2>/dev/null; rmmod lfdbus 2>/dev/null
dmesg_reset
check "lfdbus loads" insmod ./lfdbus.ko
check "bus registered in sysfs" test -d $B/devices -a -d $B/drivers
check "hotplug a temp device with hwid 7" sh -c "echo 'temp 7' > $B/new_device"
check "lfd7 appears on the bus" test -e $B/devices/lfd7
check_grep "lfd7 lives below the lfdbus root device" "devices/lfdbus/lfd7" readlink -f $B/devices/lfd7
check_grep "type attribute" "^temp$" cat $B/devices/lfd7/type
check_grep "modalias attribute" "^lfd:temp$" cat $B/devices/lfd7/modalias
check_grep "uevent carries MODALIAS and LFD_HWID" "LFD_HWID=7" cat $B/devices/lfd7/uevent
check_grep "uevent MODALIAS" "MODALIAS=lfd:temp" cat $B/devices/lfd7/uevent
check "no driver bound yet" test ! -e $B/devices/lfd7/driver
check_fail "duplicate hwid rejected" sh -c "echo 'temp 7' > $B/new_device"
check "load lfd_temp" insmod ./lfd_temp.ko
check_grep "lfd7 bound to lfd_temp" "lfd_temp$" readlink $B/devices/lfd7/driver
check_grep "temperature = 20000 + 100*7" "^20700$" cat $B/devices/lfd7/temperature
check "offset is writable" sh -c "echo -200 > $B/devices/lfd7/offset"
check_grep "temperature follows the offset" "^20500$" cat $B/devices/lfd7/temperature
check "a humid device stays unbound" sh -c "echo 'humid 8' > $B/new_device && test ! -e $B/devices/lfd8/driver"
check "manual unbind" sh -c "echo lfd7 > $B/drivers/lfd_temp/unbind"
check "attributes gone after unbind" test ! -e $B/devices/lfd7/temperature
check "manual bind" sh -c "echo lfd7 > $B/drivers/lfd_temp/bind"
check_grep "state is fresh after rebind (offset 0)" "^0$" cat $B/devices/lfd7/offset
check "delete lfd7" sh -c "echo 7 > $B/delete_device"
check_grep "remove() ran and the device was released" "released lfd7" dmesg_since
check_fail "deleting a missing device fails" sh -c "echo 7 > $B/delete_device"

# Autoloading the way udev does it: modprobe the MODALIAS.
M=/lib/modules/$(uname -r)/extra
mkdir -p $M && cp lfdbus.ko lfd_temp.ko $M/ && depmod -a
check "rmmod lfd_temp" unload lfd_temp
echo 'temp 9' > $B/new_device
check "modprobe \$(cat modalias) loads the right driver" sh -c "modprobe \$(cat $B/devices/lfd9/modalias)"
check "lfd9 is bound after autoload" test -e $B/devices/lfd9/driver
check "rmmod lfd_temp" unload lfd_temp
check "rmmod lfdbus removes the remaining devices" unload lfdbus
check_grep "every device was released" "released lfd9" dmesg_since
check_clean_log all
summary
