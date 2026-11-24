#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 21 acceptance tests: QEMU's RTL8139 on the user-mode network.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfd8139 2>/dev/null
dmesg_reset
check_fail "no in-tree 8139 driver is bound" sh -c "ls /sys/bus/pci/drivers | grep -q 8139too"
check "lfd8139 loads" insmod ./lfd8139.ko
check_grep "probe message with QEMU's default MAC" "MAC 52:54:00:12:34:56" dmesg_since
ifc=$(ls /sys/bus/pci/drivers/lfd8139/*/net 2>/dev/null | head -1)
check "a network interface was created ($ifc)" test -n "$ifc"
check "interface up" ip link set "$ifc" up
check "carrier detected from MediaStatus" wait_until 3 grep -q 1 /sys/class/net/$ifc/carrier
ip addr add 10.0.2.15/24 dev "$ifc"
ip route add default via 10.0.2.2
check_grep "ping the slirp gateway 10.0.2.2" " 0% packet loss" ping -c 5 -W 2 10.0.2.2
cd /tmp
check "TFTP download of 1 MiB from 10.0.2.2" tftp -g -r blob.bin 10.0.2.2
check "the file is intact (md5)" sh -c "cd /tmp && md5sum -c /lab/07-network/lab21-rtl8139/test/tftp/blob.md5"
cd - >/dev/null
rx=$(cat /sys/class/net/$ifc/statistics/rx_packets)
tx=$(cat /sys/class/net/$ifc/statistics/tx_packets)
check "statistics count the traffic (rx $rx, tx $tx)" test "$rx" -gt 2000 -a "$tx" -gt 2000
check_grep "no RX/TX errors" "^0$" cat /sys/class/net/$ifc/statistics/rx_errors
check "down and up again" sh -c "ip link set $ifc down && ip link set $ifc up && ip route add default via 10.0.2.2"
check_grep "still pings after the restart" " 0% packet loss" ping -c 3 -W 2 10.0.2.2
check "lfd8139 unloads" unload lfd8139
check_clean_log all
summary
