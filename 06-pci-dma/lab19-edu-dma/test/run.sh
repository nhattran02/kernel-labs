#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 19 acceptance tests (QEMU with -device edu).  CONFIG_DMA_API_DEBUG is
# on in the lab kernel: any DMA-API misuse shows up as a WARNING.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfd_edu 2>/dev/null
dmesg_reset
for mode in streaming coherent; do
	check "[$mode] lfd_edu loads" insmod ./lfd_edu.ko mode=$mode
	check_grep "[$mode] probe message" "$mode DMA, mask 28 bits" dmesg_since
	if [ $mode = coherent ]; then
		bus=$(dmesg | sed -n 's/.*coherent buffer at bus address \(0x[0-9a-f]*\).*/\1/p' | tail -1)
		check "[$mode] the coherent buffer is below 256 MiB ($bus)" test $((bus)) -lt $((0x10000000))
	fi
	run_prog "[$mode] dma_test" ./test/dma_test /dev/edu0
	check "[$mode] lfd_edu unloads" unload lfd_edu
	check_clean_log
	dmesg_mark
done
check "DMA-API debugging is active (CONFIG_DMA_API_DEBUG)" test -d /sys/kernel/debug/dma-api
check_grep "DMA-API debugging counted no errors" "^0$" cat /sys/kernel/debug/dma-api/error_count
summary
