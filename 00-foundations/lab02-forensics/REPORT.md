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

## Report 4: BUG splat on the console on the first command

**Evidence**
```
# sh test/repro.sh 4
BUG: sleeping function called from invalid context at include/linux/sched/mm.h:321
in_atomic(): 1, irqs_disabled(): 0, non_block: 0, pid: 1, name: sh
preempt_count: 1, expected: 0
2 locks held by sh/1:
 #0: ff11000001acc3f8 (sb_writers#8){.+.+}-{0:0}, at: ksys_write+0xf5/0x1e0
 #1: ffffffffc0408098 (stats_lock){+.+.}-{2:2}, at: record_command+0x16/0x120 [lfd_buggy]
Call Trace:
 __might_resched+0x2f8/0x510
 __kmalloc_node_track_caller_noprof+0x346/0x470
 krealloc_noprof+0x5f/0x120
 record_command+0xb2/0x120 [lfd_buggy]
 ctl_write+0x21a/0x260 [lfd_buggy]
```

**Tool:** `CONFIG_DEBUG_ATOMIC_SLEEP` with lockdep. The bug only hurts
when the allocator really has to reclaim memory and sleeps, which manual
testing never hits. `might_alloc()` (mm.h:321) warns whenever a
`GFP_KERNEL` allocation *could* sleep in atomic context; lockdep names
the lock that is held (`stats_lock`), and `preempt_count: 1` is that
spinlock.

**Root cause:** `lfd_buggy.c:59-61`. `record_command()` calls
`krealloc(GFP_KERNEL)` while holding the `stats_lock` spinlock. Rule: do
not sleep while holding a spinlock. It fires on the first command and
then every 16th, when the history array grows.

**Fix:** make `stats_lock` a mutex. Both users, `record_command()` (from
`write()`) and `stats_show()` (from `read()`), run in process context,
so nothing needs a spinlock. `GFP_ATOMIC` would only hide the warning and
draw on the emergency reserves for a statistic.

**Verification:** `test/run.sh` checks 10 and 11: no "sleeping function
called from invalid context", and stats count every command.

## Report 5: all commands hang after a failed rename

**Evidence**
```
# sh test/repro.sh 5
================================================
WARNING: lock held when returning to user space!
------------------------------------------------
sh/1 is leaving the kernel with locks still held!
1 lock held by sh/1:
 #0: ... (items_lock){+.+.}-{...}, at: cmd_rename+0x.../0x... [lfd_buggy]
```
The next command that needs `items_lock` (`echo 'add z' > ctl &`) never
returns.

**Tool:** lockdep. A missing unlock does not fail where it happens; the
next caller just blocks forever and the hung-task detector only says
who is waiting. Lockdep checks on every return to user space that no
lock is still held, and names the lock and where it was taken.

**Root cause:** `lfd_buggy.c:177-180`. The not-found path of
`cmd_rename()` frees the new name and returns `-ENOENT` with
`items_lock` still held. Rule: every exit path after `mutex_lock()` must
unlock.

**Fix:** unlock before returning `-ENOENT`.

**Verification:** `test/run.sh` checks 14-16: rename of a missing item
fails, later commands still work, and no "lock held when returning to
user space".

## Report 6: the shell dies when looking up a missing item

**Evidence**
```
# sh test/repro.sh 6
Oops: general protection fault, probably for non-canonical address 0xdffffc0000000004: 0000 [#1] PREEMPT SMP KASAN NOPTI
KASAN: null-ptr-deref in range [0x0000000000000020-0x0000000000000027]
RIP: 0010:cmd_find+0xa9/0x130 [lfd_buggy]
```
`0xdffffc0000000004` is the KASAN shadow of address `0x20`
(`(0x20 >> 3) + 0xdffffc0000000000`), and 0x20 is
`offsetof(struct item, id)`: the code read `NULL->id`.

**Tool:** the oops itself, decoded with KASAN and `faddr2line`. RIP gives
the function and offset; KASAN turns the odd non-canonical address into
the real range. Without KASAN the same bug shows as
`BUG: kernel NULL pointer dereference, address: 0000000000000020`. The
oops kills the current task, which is `sh`, PID 1 in the guest.

**Root cause:** `lfd_buggy.c:140`. `cmd_find()` dereferences the result
of `find_item()` without checking it, although `find_item()` returns
NULL when there is no such item. Rule: check every lookup result before
using it.

**Fix:** return `-ENOENT` when the lookup fails.

**Verification:** `test/run.sh` checks 12 and 13: no oops, and find on a
missing item reports `ENOENT`.
