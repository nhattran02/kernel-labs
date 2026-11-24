#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 20 acceptance tests.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfdnet 2>/dev/null
dmesg_reset
S=/sys/class/net
check "lfdnet loads" insmod ./lfdnet.ko
check "lfd0 and lfd1 exist" test -d $S/lfd0 -a -d $S/lfd1
check_grep "Ethernet type (ARPHRD_ETHER = 1)" "^1$" cat $S/lfd0/type
check "bring lfd0 up" ip link set lfd0 up
check_grep "no carrier while the peer is down" "^0$" cat $S/lfd0/carrier
check "bring lfd1 up" ip link set lfd1 up
check_grep "carrier on both ends" "^1$" cat $S/lfd0/carrier
check_grep "operstate up" "^up$" cat $S/lfd1/operstate
run_prog "AF_PACKET + ethtool test" ./test/net_test
tx=$(cat $S/lfd0/statistics/tx_packets); rx=$(cat $S/lfd1/statistics/rx_packets)
check "tx_packets(lfd0) = $tx and rx_packets(lfd1) = $rx agree" test "$tx" -eq "$rx" -a "$rx" -ge 2000
check "bring lfd1 down" ip link set lfd1 down
check_grep "lfd0 loses its carrier" "^0$" cat $S/lfd0/carrier
check "set the MTU to 1400" ip link set lfd0 mtu 1400
check_grep "MTU applied" "^1400$" cat $S/lfd0/mtu
check_fail "MTU above 1500 is refused" ip link set lfd0 mtu 9000
check "lfdnet unloads (interfaces up or down)" unload lfdnet
check "interfaces gone" test ! -e $S/lfd0
check_clean_log all
summary
