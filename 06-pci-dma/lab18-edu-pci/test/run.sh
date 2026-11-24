#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 18 acceptance tests (QEMU with -device edu).
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
dev=$(grep -l "0x11e8" /sys/bus/pci/devices/*/device 2>/dev/null | head -1)
dev=${dev%/device}
bdf=${dev##*/}
check "an edu device is present" test -n "$bdf"
[ -n "$bdf" ] || { summary; exit 1; }
DRV=/sys/bus/pci/drivers/lfd_edu
rmmod lfd_edu 2>/dev/null
dmesg_reset

check_grep "vendor 0x1234" "^0x1234$" cat $dev/vendor
check_grep "class 0x00ff00 (unclassified)" "^0x00ff00$" cat $dev/class
check_grep "BAR0: 1 MiB, memory, 32-bit" "^0x[0-9a-f]+ 0x[0-9a-f]+ 0x0*40200$" sed -n 1p $dev/resource
check_grep "interrupt pin A (config byte 0x3d)" "^01$" sh -c "dd if=$dev/config bs=1 skip=61 count=1 2>/dev/null | hexdump -e '1/1 \"%02x\"'"
check "lfd_edu loads" insmod ./lfd_edu.ko
check_grep "probe message" "edu v1.0, MSI IRQ [0-9]+, /dev/edu0" dmesg_since
check_grep "irq_mode attribute says MSI" "^MSI$" cat $dev/irq_mode
check "bus master enabled (command register bit 2)" \
	sh -c "[ \$((0x\$(dd if=$dev/config bs=1 skip=4 count=1 2>/dev/null | hexdump -e '1/1 \"%02x\"') & 4)) -ne 0 ]"
run_prog "edu_test" ./test/edu_test /dev/edu0

./test/edu_test gone /dev/edu0 3 > /tmp/gone.out &
sleep 1
check "unbind while a file is open" sh -c "echo $bdf > $DRV/unbind"
check "/dev/edu0 disappears at once" test ! -e /dev/edu0
wait
check_grep "the open file now gets ENODEV (no crash)" "ENODEV" cat /tmp/gone.out
check "bind again" sh -c "echo $bdf > $DRV/bind"
check "/dev/edu0 is back" test -c /dev/edu0
run_prog "edu_test after rebind" ./test/edu_test /dev/edu0

check "hot-remove the PCI device" sh -c "echo 1 > $dev/remove"
check "the device is gone from the bus" test ! -e $dev
check "rescan the bus" sh -c "echo 1 > /sys/bus/pci/rescan"
check "re-enumerated and probed again" wait_until 5 test -c /dev/edu0
run_prog "edu_test after rescan" ./test/edu_test /dev/edu0

check "lfd_edu unloads" unload lfd_edu
check "lfd_edu loads with force_intx=1" insmod ./lfd_edu.ko force_intx=1
check_grep "irq_mode attribute says INTx" "^INTx$" cat $dev/irq_mode
run_prog "edu_test on INTx" ./test/edu_test /dev/edu0
check "lfd_edu unloads" unload lfd_edu
check_clean_log all
summary
