#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 12 acceptance tests: bind edu to uio_pci_generic and drive it from
# user space.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
dev=$(grep -l "0x11e8" /sys/bus/pci/devices/*/device 2>/dev/null | head -1)
dev=${dev%/device}
bdf=${dev##*/}
check "an edu device is present" test -n "$bdf"
[ -n "$bdf" ] || { summary; exit 1; }
dmesg_reset
check "uio_pci_generic loads" modprobe uio_pci_generic
G=/sys/bus/pci/drivers/uio_pci_generic
[ -e $dev/driver ] && echo "$bdf" > $dev/driver/unbind
check "teach uio_pci_generic the edu ID (new_id)" sh -c "echo '1234 11e8' > $G/new_id"
check "edu is now bound to uio_pci_generic" test -e $G/$bdf
check_grep "a /dev/uioN node exists" "uio[0-9]" ls $dev/uio
check_grep "100 interrupts handled in user space" "^raised 100 handled 100 " ./edu_uio $bdf raise 100
./edu_uio $bdf raise 100
check_grep "10! = 3628800 from user space" "^fact 10 = 3628800$" ./edu_uio $bdf fact 10
check_grep "the UIO interrupt shows in /proc/interrupts" "uio_pci_generic" cat /proc/interrupts
check "unbind" sh -c "echo $bdf > $G/unbind && echo '1234 11e8' > $G/remove_id"
check "uio_pci_generic unloads" unload uio_pci_generic
check_clean_log all
summary
