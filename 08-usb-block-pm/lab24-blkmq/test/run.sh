#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 24 acceptance tests.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
B=/sys/block/lfdblk0
rmmod lfdblk 2>/dev/null
dmesg_reset
head -c 3000000 /dev/urandom > /tmp/payload
md5=$(md5sum /tmp/payload | cut -d' ' -f1)
mkdir -p /mnt

for delay in 0 200; do
	tag="[delay_us=$delay]"
	check "$tag lfdblk loads" insmod ./lfdblk.ko capacity_mb=32 nr_queues=2 delay_us=$delay
	check "$tag /dev/lfdblk0 exists" test -b /dev/lfdblk0
	check_grep "$tag size is 32 MiB = 65536 sectors" "^65536$" cat $B/size
	check_grep "$tag two hardware queues" "^0 1$" sh -c "ls $B/mq | xargs"
	check "$tag mke2fs" mke2fs -q /dev/lfdblk0
	check "$tag mount" mount -t ext2 /dev/lfdblk0 /mnt
	check "$tag copy 3 MB" cp /tmp/payload /mnt/payload
	check "$tag umount (flushes everything)" umount /mnt
	echo 3 > /proc/sys/vm/drop_caches
	check "$tag mount again" mount -t ext2 /dev/lfdblk0 /mnt
	check_grep "$tag the file survived, bit for bit" "^$md5 " md5sum /mnt/payload
	umount /mnt
	check "$tag direct I/O write of 1 MiB" dd if=/tmp/payload of=/dev/lfdblk0 bs=64k count=16 oflag=direct
	check "$tag unload" unload lfdblk
done

check "fault injection: load with fail_sector=1000" insmod ./lfdblk.ko fail_sector=1000
check "sector 999 reads fine" dd if=/dev/lfdblk0 of=/dev/null bs=512 skip=999 count=1 iflag=direct
check_fail "sector 1000 fails" dd if=/dev/lfdblk0 of=/dev/null bs=512 skip=1000 count=1 iflag=direct
check_grep "the block layer logged the I/O error" "I/O error, dev lfdblk0, sector 1000" dmesg_since
echo -1 > /sys/module/lfdblk/parameters/fail_sector
check "discard (blkdiscard) is supported" blkdiscard /dev/lfdblk0
check "unload" unload lfdblk
check_clean_log all
summary
