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

## Report 2: random corruption after adding items

**Evidence**
```
BUG: KASAN: slab-out-of-bounds in strcpy+0x9f/0xb0
Write of size 1 at addr ff11000003283ac6 by task sh/83
 strcpy+0x9f/0xb0
 cmd_add+0xa1/0x340 [lfd_buggy]
Allocated by task 83:
 __kmalloc_noprof+0x199/0x480
 cmd_add+0x6e/0x340 [lfd_buggy]
The buggy address is located 0 bytes to the right of
 allocated 6-byte region [ff11000003283ac0, ff11000003283ac6)
```
`add victim`: 6 bytes allocated for a 6-character name, then a 1-byte
write just past the end. `scripts/faddr2line lfd_buggy.ko
cmd_add+0xa1/0x340` points at the `strcpy()` line.

**Tool:** KASAN. The overflow does not fault at the faulty write; it
silently corrupts whatever follows, and the crash comes later somewhere
unrelated. KASAN checks every access against shadow memory and reports
at the exact write, with the allocation site.

**Root cause:** `lfd_buggy.c:91`. `kmalloc(strlen(name))` leaves no room
for the terminating NUL that `strcpy()` writes. Rule: a C string needs
`strlen + 1` bytes. Without KASAN the NUL usually lands in the slack of
the kmalloc-8/16/32 object; with a name of exactly 8, 16, 32...
characters (`repro.sh 2` uses `abcdefgh`) it overwrites the first byte
of the neighbouring object.

**Fix:** allocate `len + 1` bytes.

**Verification:** `test/run.sh` checks 6 and 7: no KASAN
slab-out-of-bounds, and item `abcdefgh` is listed intact.

## Report 3: garbage in the log after deleting

**Evidence**
```
# sh test/repro.sh 3
BUG: KASAN: slab-use-after-free in cmd_del+0x.../0x... [lfd_buggy]
Read of size ... at addr ... by task sh/1
 cmd_del+0x.../0x... [lfd_buggy]
 ctl_write+0x.../0x... [lfd_buggy]
Freed by task 1:
 kfree+...
 cmd_del+0x.../0x... [lfd_buggy]
```
The access and the free are in the same function, a few lines apart.

**Tool:** KASAN. Without it the read returns whatever the allocator left
in the freed object, so the log shows garbage instead of crashing. KASAN
poisons freed objects and keeps them in quarantine, so the first read
after `kfree()` is reported together with the alloc and free stacks.

**Root cause:** `lfd_buggy.c:128`. `cmd_del()` prints `it->name` and
`it->id` after `kfree(it->name)` and `kfree(it)`. Rule: never touch an
object after freeing it.

**Fix:** print the message before freeing. The item is already off the
list, so only this function can reach it and no lock is needed.

**Verification:** `test/run.sh` checks 8 and 9: no KASAN use-after-free,
and the deletion is logged with the right name.
