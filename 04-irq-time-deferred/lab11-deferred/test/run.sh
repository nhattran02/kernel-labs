#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 11 acceptance tests.  Latencies are printed for your report; the checks
# only test what must hold everywhere (all events processed, right context).
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
D=/sys/kernel/debug/lfddefer
rmmod lfddefer 2>/dev/null
dmesg_reset
check "lfddefer loads" insmod ./lfddefer.ko
check_grep "the kthread exists and is SCHED_FIFO" "^FF|FIFO|^1$" \
	sh -c "cat /proc/\$(pidof lfddefer)/stat | awk '{print \$41}'"

table=/tmp/lfddefer.table
: > $table
for load in idle busy; do
	if [ $load = busy ]; then
		( while :; do :; done ) & L1=$!
		( while :; do :; done ) & L2=$!
	fi
	for m in tasklet bh wq highpri kthread; do
		if echo "$m 2000 1000" > $D/run; then
			r=$(cat $D/results)
			echo "$load $r" | tee -a $table
			check_grep "[$load/$m] all 2000 events processed" "events=2000 " echo "$r"
			case $m in
			tasklet|bh) ctx=softirq ;;
			*) ctx=task ;;
			esac
			check_grep "[$load/$m] runs in $ctx context" "ctx=$ctx " echo "$r"
		else
			check "[$load/$m] run completed" false
		fi
	done
	[ $load = busy ] && kill $L1 $L2
done
check_fail "unknown mode rejected" sh -c "echo 'irq 10' > $D/run"
check "lfddefer unloads" unload lfddefer
check_clean_log all
summary
