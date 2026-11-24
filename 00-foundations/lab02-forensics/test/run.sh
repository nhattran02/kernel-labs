#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 02 acceptance tests: tools work, and none of the six defects remains.
# Each defect has its own check; operations that hang on the buggy driver
# run in the background so that the remaining checks still execute.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
D=/sys/kernel/debug/lfd_buggy
T=/sys/kernel/tracing
rmmod lfd_buggy 2>/dev/null
quiet_console
dmesg_reset
check "lfd_buggy loads" insmod ./lfd_buggy.ko

# --- Tools ---------------------------------------------------------------
echo 'module lfd_buggy +p' > /sys/kernel/debug/dynamic_debug/control
echo "add alpha" > $D/ctl
check_grep "dynamic debug enables pr_debug() of the module" "added alpha as" dmesg_since
echo 'module lfd_buggy -p' > /sys/kernel/debug/dynamic_debug/control
echo ':mod:lfd_buggy' > $T/set_ftrace_filter
echo function > $T/current_tracer
echo > $T/trace
echo "add beta" > $D/ctl
check_grep "ftrace sees cmd_add() called from ctl_write()" "cmd_add <-ctl_write" cat $T/trace
echo nop > $T/current_tracer
echo > $T/set_ftrace_filter

# --- Report 1: leak ----------------------------------------------------------
echo clear > /sys/kernel/debug/kmemleak
for i in 1 2 3 4; do echo "tag ghost$i red" > $D/ctl 2>/dev/null; done
check_fail "tag on a missing item fails" sh -c "echo 'tag ghost red' > $D/ctl"
sleep 6
echo scan > /sys/kernel/debug/kmemleak; echo scan > /sys/kernel/debug/kmemleak
check "report 1: kmemleak finds no leak from lfd_buggy" \
	sh -c "! grep -q lfd_buggy /sys/kernel/debug/kmemleak"

# --- Report 2: out-of-bounds write ------------------------------------------
dmesg_mark
echo "add abcdefgh" > $D/ctl
check "report 2: no KASAN slab-out-of-bounds" sh -c "! dmesg | grep -q slab-out-of-bounds"
check_grep "item abcdefgh is listed intact" " abcdefgh -$" cat $D/items

# --- Report 3: use after free -------------------------------------------------
dmesg_mark
echo "del abcdefgh" > $D/ctl
check "report 3: no KASAN use-after-free" sh -c "! dmesg | grep -q use-after-free"
check_grep "deletion is logged with the right name" "deleted abcdefgh \(id" dmesg_since

# --- Report 4: sleeping in atomic context -------------------------------------
for i in $(seq 1 20); do echo "find alpha" > $D/ctl; done
check "report 4: no 'sleeping function called from invalid context'" \
	sh -c "! dmesg | grep -q 'sleeping function called from invalid context'"
check_grep "stats count every command" "^commands (2[0-9]|3[0-9])$" cat $D/stats

# --- Report 6: NULL dereference -------------------------------------------------
dmesg_mark
( echo "find ghost" > $D/ctl 2>/tmp/l02_find_err; echo $? > /tmp/l02_find_rc ) &
wait_until 30 sh -c "[ -f /tmp/l02_find_rc ] || dmesg | grep -Eq 'NULL pointer|general protection|null-ptr-deref'"
check "report 6: no oops when looking up a missing item" \
	sh -c "! dmesg | grep -Eq 'NULL pointer|general protection|null-ptr-deref'"
check_grep "report 6: find on a missing item reports ENOENT" "No such file" cat /tmp/l02_find_err

# --- Report 5: lock leak --------------------------------------------------------
dmesg_mark
check_fail "rename of a missing item fails" sh -c "echo 'rename ghost x' > $D/ctl"
( echo "add gamma" > $D/ctl; touch /tmp/l02_add_done ) &
wait_until 5 test -f /tmp/l02_add_done
if [ -f /tmp/l02_add_done ]; then hung=; else hung=1; fi
check "report 5: commands still work after a failed rename" test -z "$hung"
# lockdep switches itself off once the kernel is tainted (e.g. by a KASAN
# report or an oops): only judge its silence while it is still active.
if grep -q "debug_locks: *1" /proc/lockdep_stats; then
	check "report 5: no 'lock held when returning to user space'" \
		sh -c "! dmesg | grep -q 'lock held when returning'"
else
	echo "# report 5: lockdep was disabled by an earlier taint; not checked"
fi

if [ -z "$hung" ] && [ -f /tmp/l02_find_rc ]; then
	check "lfd_buggy unloads" unload lfd_buggy
	check_clean_log all
else
	echo "# not unloading: a command is still blocked in the driver"
fi
summary
