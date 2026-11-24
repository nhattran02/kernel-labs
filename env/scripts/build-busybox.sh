#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# build-busybox.sh - build a static BusyBox for a lab target.
#
#   env/scripts/build-busybox.sh x86_64|arm64|bbb
#
# On x86_64 you may skip this and use the distribution package instead
# (Ubuntu: busybox-static); mk-initramfs.sh picks /bin/busybox if it is
# statically linked.  Result: $LFD_IMAGES/<target>/busybox
set -euo pipefail
. "$(dirname "$0")/common.sh"

target=${1:?usage: build-busybox.sh <x86_64|arm64|bbb>}
lfd_target "$target"
ver=1_36_1
src=$LFD_IMAGES/src/busybox-$ver
out=$LFD_IMAGES/$target/busybox-build

if [ ! -d "$src" ]; then
	mkdir -p "$LFD_IMAGES/src"
	git clone --depth 1 --branch "$ver" https://github.com/mirror/busybox.git "$src"
fi
mkdir -p "$out"
make -C "$src" O="$out" CROSS_COMPILE="$LFD_CROSS" defconfig >/dev/null
# Static binary; 'tc' does not build against recent kernel headers.
# Full modutils (insmod/rmmod/modprobe/depmod), not "modprobe-small": its
# rmmod reports failures but still exits with status 0.
sed -i -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' \
       -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
       -e 's/^CONFIG_MODPROBE_SMALL=y/# CONFIG_MODPROBE_SMALL is not set/' "$out/.config"
# Turning MODPROBE_SMALL off exposes the full modutils options; set the ones
# the labs rely on explicitly (Lab 13 autoloads by MODALIAS, which needs
# modules.alias) and take the Kconfig default for every other new one.
(yes "" || true) | make -C "$out" CROSS_COMPILE="$LFD_CROSS" oldconfig >/dev/null
for o in FEATURE_MODUTILS_ALIAS FEATURE_MODUTILS_SYMBOLS FEATURE_MODPROBE_BLACKLIST; do
	sed -i "s/^# CONFIG_$o is not set/CONFIG_$o=y/" "$out/.config"
done
(yes "" || true) | make -C "$out" CROSS_COMPILE="$LFD_CROSS" oldconfig >/dev/null
make -C "$out" CROSS_COMPILE="$LFD_CROSS" -j"$(nproc)" busybox
cp "$out/busybox" "$LFD_IMAGES/$target/busybox"
info "built $LFD_IMAGES/$target/busybox"
file "$LFD_IMAGES/$target/busybox" 2>/dev/null || true
