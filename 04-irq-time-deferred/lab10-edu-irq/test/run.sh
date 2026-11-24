#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 10 acceptance tests.  Needs QEMU's edu device (make qemu-test adds it).
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
dev=$(grep -l "0x11e8" /sys/bus/pci/devices/*/device 2>/dev/null | head -1)
dev=${dev%/device}
check "an edu device is present (QEMU -device edu)" test -n "$dev"
[ -n "$dev" ] || { summary; exit 1; }
echo "# edu at $dev"
rmmod lfd_edu_irq 2>/dev/null
dmesg_reset

for mode in INTx MSI; do
	[ $mode = MSI ] && p="use_msi=1" || p=""
	check "[$mode] module loads and binds" insmod ./lfd_edu_irq.ko $p
	check_grep "[$mode] probe message" "lfd_edu_irq.*IRQ [0-9]+ \($mode\)" dmesg_since
	check_grep "[$mode] handler listed in /proc/interrupts" "lfd-edu" cat /proc/interrupts
	before=$(sed -n 's/^irqs \([0-9]*\).*/\1/p' $dev/stats)
	i=0
	while [ $i -lt 100 ]; do echo 1 > $dev/raise; i=$((i + 1)); done
	after=$(sed -n 's/^irqs \([0-9]*\).*/\1/p' $dev/stats)
	check "[$mode] 100 raises -> 100 handled interrupts ($before -> $after)" \
		test "$((after - before))" -eq 100
	echo 0x80000000 > $dev/raise
	check_grep "[$mode] the handler saw the raised value" "last 0x80000000" cat $dev/stats
	check_grep "[$mode] latency statistics collected" "lat_us min [0-9]+ avg [0-9]+ max [0-9]+" cat $dev/stats
	cat $dev/stats
	echo 10 > $dev/fact
	check_grep "[$mode] 10! = 3628800" "^3628800$" cat $dev/fact
	echo 12 > $dev/fact
	check_grep "[$mode] 12! = 479001600" "^479001600$" cat $dev/fact
	echo 13 > $dev/fact
	check_grep "[$mode] 13! wraps in 32 bits: 1932053504" "^1932053504$" cat $dev/fact
	grep lfd-edu /proc/interrupts
	check "[$mode] module unloads" unload lfd_edu_irq
done
check_clean_log all
summary
