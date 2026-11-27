# Lab 02 - incident reports for lfd_buggy

For each customer report write: **evidence** (the decisive lines of the
tool output), **tool** (why this tool and not another), **root cause**
(file:line and the rule that was broken), **fix**, and **verification**.

Line numbers in **root cause** refer to the original `lfd_buggy.c`.
Reproduce each report with `sh test/repro.sh N`; `test/run.sh` checks all
of them.

## Report 1: memory use grows when tagging missing items

**Evidence**
```
# sh test/repro.sh 1
# echo scan > /sys/kernel/debug/kmemleak; sleep 6
# echo scan > /sys/kernel/debug/kmemleak
kmemleak: 1 new suspected memory leaks (see /sys/kernel/debug/kmemleak)
# cat /sys/kernel/debug/kmemleak
unreferenced object 0xff110000026463c0 (size 8):
  comm "sh", pid 1, jiffies 4294953790
  hex dump (first 8 bytes):
    72 65 64 00 00 00 11 ff                          red.....
  backtrace (crc da45747e):
    [<000000004d85f1a2>] __kmalloc_noprof+0x35d/0x420
    [<00000000689b50b6>] cmd_tag+0x2a/0xd0 [lfd_buggy]
    [<00000000583bd7d3>] ctl_write+0x23c/0x260 [lfd_buggy]
```
The 8-byte object holds the tag string `"red"` and was allocated in
`cmd_tag()`.

**Tool:** kmemleak. A leak does not crash anything; the memory is just
never freed. kmemleak scans memory for pointers and reports allocated
objects that nothing refers to any more (after they have aged > 5 s, so
scan twice).

**Root cause:** `lfd_buggy.c:150-158`. `cmd_tag()` duplicates the tag
before looking the item up, and the not-found path (`goto out`) returns
`-ENOENT` without freeing it. Rule: every exit path must free what the
function still owns.

**Fix:** free the buffer on the not-found path. On success the item takes
ownership of it, and the previous tag is freed before it is replaced
(`kfree(NULL)` is a no-op for the first tag).

**Verification:** `test/run.sh` checks 4 and 5: tag on a missing item
fails with `-ENOENT` and kmemleak finds no leak from lfd_buggy.
