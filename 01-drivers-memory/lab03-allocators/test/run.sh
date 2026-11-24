#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 03 acceptance tests.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
D=/sys/kernel/debug/lfd_mem
F=/sys/kernel/debug/failslab
rmmod lfd_mem 2>/dev/null
dmesg_reset
check "lfd_mem loads" insmod ./lfd_mem.ko

# T1: kmalloc size classes (x86_64, SLUB, 4 KiB pages).
check_grep "kmalloc(1) is served from kmalloc-8"     "^ +1 +8 " cat $D/sizes
check_grep "kmalloc(33) is served from kmalloc-64"   "^ +33 +64 " cat $D/sizes
check_grep "kmalloc(100) is served from kmalloc-128" "^ +100 +128 " cat $D/sizes
check_grep "kmalloc(200) is served from kmalloc-256" "^ +200 +256 " cat $D/sizes
check_grep "kmalloc(5000) gets 8192 bytes"           "^ +5000 +8192 " cat $D/sizes
check_grep "kmalloc(10000) gets 16384 bytes"         "^ +10000 +16384 " cat $D/sizes

# T2: physical contiguity.
check_grep "kmalloc(64K) is physically contiguous"     "^kmalloc .*linear map\) pfn-breaks 0$" cat $D/contig
check_grep "alloc_pages(order 4) is contiguous"        "^alloc_pages .*linear map\) pfn-breaks 0$" cat $D/contig
check_grep "vmalloc() memory lives in the vmalloc area" "^vmalloc .*vmalloc area" cat $D/contig
check_grep "kvmalloc(8M) falls back to vmalloc"        "^kvmalloc-8M .*vmalloc area" cat $D/contig

# T3: limits.
check_grep "KMALLOC_MAX_SIZE is 4 MiB"          "KMALLOC_MAX_SIZE 4194304" cat $D/limits
check_grep "kmalloc(KMALLOC_MAX_SIZE) works"    "^kmalloc\(max\) ok" cat $D/limits
check_grep "kmalloc(KMALLOC_MAX_SIZE+1) fails"  "^kmalloc\(max\+1\) FAILED" cat $D/limits
check_grep "alloc_pages(MAX_PAGE_ORDER+1) fails" "^alloc_pages\(max_order\+1\) FAILED" cat $D/limits

# T4: timing.
check_grep "timing reports three allocators" "alloc_page" cat $D/timing

# T5: dedicated cache.
check "lfd_req cache exists in sysfs" test -d /sys/kernel/slab/lfd_req
check "lfd_req is not merged with another cache (ctor)" test ! -L /sys/kernel/slab/lfd_req
osz=$(sed -n 's/^object_size //p' $D/cache)
check_grep "slab object_size equals sizeof(struct lfd_req) ($osz)" "^$osz$" cat /sys/kernel/slab/lfd_req/object_size
check_grep "objects are cache-line aligned (64 bytes)" "^64$" cat /sys/kernel/slab/lfd_req/align
echo "# lfd_req: slab_size $(cat /sys/kernel/slab/lfd_req/slab_size)," \
     "objs_per_slab $(cat /sys/kernel/slab/lfd_req/objs_per_slab)," \
     "order $(cat /sys/kernel/slab/lfd_req/order)"
check "allocate 500 objects" sh -c "echo 'alloc 500' > $D/cache"
check_grep "500 objects held" "^held 500$" cat $D/cache
check_grep "slab reports at least 500 objects" "^(5[0-9][0-9]|[6-9][0-9][0-9]|[0-9]{4,})" cat /sys/kernel/slab/lfd_req/objects
check "free 500 objects" sh -c "echo 'free 500' > $D/cache"
check_grep "nothing held" "^held 0$" cat $D/cache

# T6: mempool under injected slab failures.
check "pool without fault injection" sh -c "echo 'pool 32' > $D/cache"
check_grep "32 of 32 allocations succeed" "pool: 32 of 32" dmesg_since
dmesg_mark
echo Y > $F/cache-filter
echo 1 > /sys/kernel/slab/lfd_req/failslab
echo 100 > $F/probability
echo -1 > $F/times
echo 0 > $F/verbose
check "pool with every lfd_req slab allocation failing" sh -c "echo 'pool 32' > $D/cache"
echo 0 > $F/probability
echo 0 > /sys/kernel/slab/lfd_req/failslab
check_grep "only the 16 reserved elements are handed out" "pool: 16 of 32" dmesg_since

check "lfd_mem unloads" unload lfd_mem
check "lfd_req cache destroyed" test ! -e /sys/kernel/slab/lfd_req
check_clean_log all
summary
