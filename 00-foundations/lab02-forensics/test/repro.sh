#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# repro.sh N - reproduce customer bug report N (1..6) of Lab 02.
# Load the module first:  insmod lfd_buggy.ko
C=/sys/kernel/debug/lfd_buggy/ctl
[ -w $C ] || { echo "load lfd_buggy.ko first"; exit 1; }
case "$1" in
1) echo "Report 1: 'memory use grows when tagging missing items'"
   for i in 1 2 3 4 5 6 7 8; do echo "tag ghost$i red" > $C; done
   echo "now: echo scan > /sys/kernel/debug/kmemleak (wait >5 s, scan twice)";;
2) echo "Report 2: 'random corruption after adding items'"
   echo "add abcdefgh" > $C ;;
3) echo "Report 3: 'garbage in the log after deleting'"
   echo "add victim" > $C; echo "del victim" > $C ;;
4) echo "Report 4: 'BUG splat on the console on the first command'"
   echo "add first" > $C; cat /sys/kernel/debug/lfd_buggy/stats ;;
5) echo "Report 5: 'all commands hang after a failed rename'"
   echo "rename ghost x" > $C
   echo "then try (in the background!): echo 'add z' > $C &" ;;
6) echo "Report 6: 'the shell dies when looking up a missing item'"
   echo "find ghost" > $C ;;
*) echo "usage: $0 1..6"; exit 1 ;;
esac
