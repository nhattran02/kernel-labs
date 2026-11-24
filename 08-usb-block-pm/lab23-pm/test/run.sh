#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 23 acceptance tests: runtime PM and system sleep (s2idle + pm_test).
# The /lab share (virtio-9p) does not survive system sleep in QEMU - its
# driver has no PM support, so the transport is dead after resume.  Run
# everything from a copy in /tmp.
T=/tmp/lab23
if [ "$0" != $T/run.sh ]; then
	rm -rf $T && mkdir -p $T
	cp -r "$(dirname "$0")/.." $T/lab && cp /lab/common/lib.sh $T/ && cp "$0" $T/run.sh
	exec sh $T/run.sh
fi
. $T/lib.sh
cd $T/lab || exit 1
dev=$(grep -l "0x11e8" /sys/bus/pci/devices/*/device 2>/dev/null | head -1)
dev=${dev%/device}
P=$dev/power
rmmod lfd_edu 2>/dev/null
dmesg_reset
counts() { cat $dev/pm_counts; }
check "lfd_edu loads" insmod ./lfd_edu.ko
check_grep "runtime PM: user space has not allowed it yet (control = on)" "^on$" cat $P/control
check_grep "so the device stays active" "^active$" cat $P/runtime_status
check "allow runtime PM (power/control = auto)" sh -c "echo auto > $P/control"
check "the device autosuspends when idle" wait_until 3 grep -q "^suspended$" $P/runtime_status
check_grep "runtime_suspend() ran" "rt_suspend [1-9]" counts
check_grep "an ioctl resumes it on demand: 10! = 3628800" "^3628800$" ./test/edu_fact 10
check_grep "runtime_resume() ran" "rt_resume [1-9]" counts
check "... and it suspends again afterwards" wait_until 3 grep -q "^suspended$" $P/runtime_status
check_grep "autosuspend delay is 100 ms" "^100$" cat $P/autosuspend_delay_ms
echo "# $(counts)"

check "forbid runtime PM again (control = on) - resumes the device" sh -c "echo on > $P/control"
check_grep "active" "^active$" cat $P/runtime_status
check "s2idle with pm_test=devices (suspend devices, wait 5 s, resume)" \
	sh -c "echo devices > /sys/power/pm_test && echo freeze > /sys/power/state"
echo none > /sys/power/pm_test
check_grep "system suspend() and resume() each ran once" "^suspend 1 resume 1 " counts
check_grep "the device works after resume" "^479001600$" ./test/edu_fact 12

check "allow runtime PM and let it suspend" sh -c "echo auto > $P/control"
wait_until 3 grep -q "^suspended$" $P/runtime_status
check "s2idle again, now with the device runtime-suspended" \
	sh -c "echo devices > /sys/power/pm_test && echo freeze > /sys/power/state"
echo none > /sys/power/pm_test
echo "# $(counts)"
check_grep "direct-complete: a runtime-suspended device was left alone (still suspend 1 resume 1)" \
	"^suspend 1 resume 1 " counts
check_grep "the device still works" "^5040$" ./test/edu_fact 7
check "lfd_edu unloads" unload lfd_edu
check_clean_log all
summary
