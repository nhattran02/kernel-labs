#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# run-all-tests.sh [lab-dir...] - build every lab and run its acceptance
# test in a fresh QEMU guest; print one summary line per lab.
# x86_64 labs need the x86_64 kernel/initramfs, Lab 15 the arm64 ones;
# BeagleBone labs (16, 25) are only built (TARGET=bbb).
set -u
repo=$(cd "$(dirname "$0")/.." && pwd)
labs=("$@")
[ ${#labs[@]} -eq 0 ] && mapfile -t labs < <(cd "$repo" && ls -d [0-9]*/lab*/ | sed 's:/$::')
mkdir -p "$repo/.test-logs"
for lab in "${labs[@]}"; do
	log="$repo/.test-logs/$(basename "$lab").log"
	case $lab in
	*lab16-bbb-gpio-irq|*lab25-bbb-eeprom)
		if make -C "$repo/$lab" TARGET=bbb >"$log" 2>&1; then r="BUILT (BBB: run test/run.sh on the board)"; else r="BUILD FAILED"; fi ;;
	*)
		if ! make -C "$repo/$lab" >"$log" 2>&1; then
			r="BUILD FAILED"
		else
			make -C "$repo/$lab" qemu-test >>"$log" 2>&1
			r=$(grep -E '^# [0-9]+/[0-9]+ checks passed' "$log" | tail -1)
			grep -q '^=== lfd: exit status 0' "$log" && r="PASS $r" || r="FAIL $r"
		fi ;;
	esac
	printf '%-45s %s\n' "$lab" "$r"
done
