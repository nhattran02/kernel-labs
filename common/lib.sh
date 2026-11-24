# SPDX-License-Identifier: GPL-2.0
# common/lib.sh - helpers for test/run.sh scripts (BusyBox sh compatible).
#   . /lab/common/lib.sh   (or: . "$LAB/common/lib.sh")
#   check "module loads" insmod ./foo.ko
#   check_grep "probe message" "lfd-edu.*probed" dmesg
#   summary

_n=0
_failed=0

# check DESCRIPTION COMMAND [ARGS...]: pass if the command exits with 0.
check() {
	desc=$1; shift
	_n=$((_n + 1))
	if "$@" >/tmp/.lfd_out 2>&1; then
		echo "ok $_n - $desc"
	else
		echo "not ok $_n - $desc   [$*]"
		sed 's/^/#   /' /tmp/.lfd_out | tail -5
		_failed=$((_failed + 1))
	fi
}

# check_fail DESCRIPTION COMMAND [ARGS...]: pass if the command fails.
check_fail() {
	desc=$1; shift
	_n=$((_n + 1))
	if "$@" >/tmp/.lfd_out 2>&1; then
		echo "not ok $_n - $desc   [expected failure: $*]"
		_failed=$((_failed + 1))
	else
		echo "ok $_n - $desc"
	fi
}

# check_grep DESCRIPTION REGEX COMMAND [ARGS...]: output must match REGEX.
check_grep() {
	desc=$1; re=$2; shift 2
	_n=$((_n + 1))
	if "$@" 2>&1 | grep -Eq -- "$re"; then
		echo "ok $_n - $desc"
	else
		echo "not ok $_n - $desc   [no match for /$re/ in: $*]"
		_failed=$((_failed + 1))
	fi
}

# run_prog DESCRIPTION PROGRAM [ARGS...]: run a C test program that prints
# TAP lines itself; its failures are added to ours.
run_prog() {
	desc=$1; shift
	echo "# --- $desc"
	"$@"
	rc=$?
	_n=$((_n + 1))
	if [ $rc -eq 0 ]; then echo "ok $_n - $desc"
	else echo "not ok $_n - $desc (exit $rc)"; _failed=$((_failed + 1)); fi
}

# dmesg_mark: start a new section of the kernel log.  The ring buffer is
# moved to /tmp/lfd_dmesg.all and cleared, so that dmesg_since shows only
# the new section even if a flood of reports wraps the ring buffer.
# dmesg_all: everything logged since the first dmesg_mark.
# dmesg_reset: forget everything logged so far (call once at the start).
dmesg_reset() { dmesg -c >/dev/null 2>&1; rm -f /tmp/lfd_dmesg.all; }
dmesg_mark() { dmesg -c >> /tmp/lfd_dmesg.all 2>/dev/null; }
dmesg_since() { dmesg; }
dmesg_all() { cat /tmp/lfd_dmesg.all 2>/dev/null; dmesg; }

_BAD='BUG:|WARNING:|Oops|KASAN:|circular locking|inconsistent lock|UBSAN:|suspected memory leak|refcount_t:'

# check_clean_log [all]: no warning, oops, KASAN, lockdep or DMA-API
# report since the last dmesg_mark (or since the first one, with "all").
check_clean_log() {
	if [ "$1" = all ]; then _log=dmesg_all; else _log=dmesg_since; fi
	_n=$((_n + 1))
	if $_log | grep -Eq "$_BAD"; then
		echo "not ok $_n - kernel log is clean"
		$_log | grep -E -A3 "$_BAD" | head -20 | sed 's/^/#   /'
		_failed=$((_failed + 1))
	else
		echo "ok $_n - kernel log is clean"
	fi
}

# wait_until SECONDS COMMAND [ARGS...]: poll once per second until the
# command succeeds; return 1 on timeout.
wait_until() {
	_t=$1; shift
	while [ "$_t" -gt 0 ]; do
		"$@" >/dev/null 2>&1 && return 0
		sleep 1
		_t=$((_t - 1))
	done
	return 1
}

# quiet_console: keep kernel messages off the console (they stay in dmesg).
# A flood of reports on a slow emulated serial line can take minutes.
quiet_console() { echo 1 > /proc/sys/kernel/printk; }

# run_prog_bg SECONDS DESCRIPTION PROGRAM [ARGS...]: like run_prog, but the
# program runs in the background and is abandoned (reported as a failure)
# if it has not finished after SECONDS - e.g. because a thread is stuck in
# the kernel after an oops or a deadlock.  Waiting would hang the run.
run_prog_bg() {
	_t=$1; desc=$2; shift 2
	echo "# --- $desc"
	rm -f /tmp/.lfd_bg_rc
	( "$@"; echo $? > /tmp/.lfd_bg_rc ) &
	wait_until "$_t" test -f /tmp/.lfd_bg_rc
	_n=$((_n + 1))
	if [ ! -f /tmp/.lfd_bg_rc ]; then
		echo "not ok $_n - $desc (did not finish in ${_t}s: crash or deadlock?)"
		_failed=$((_failed + 1))
		LFD_STUCK=1
	elif [ "$(cat /tmp/.lfd_bg_rc)" = 0 ]; then
		echo "ok $_n - $desc"
	else
		echo "not ok $_n - $desc (exit $(cat /tmp/.lfd_bg_rc))"
		_failed=$((_failed + 1))
	fi
}

# unload MODULE: rmmod, then verify that the module is really gone.  (Some
# BusyBox builds - CONFIG_MODPROBE_SMALL - print an error but exit with 0.)
unload() {
	rmmod "$1" && test ! -d "/sys/module/$1"
}

summary() {
	echo "# $((_n - _failed))/$_n checks passed"
	[ $_failed -eq 0 ]
}
