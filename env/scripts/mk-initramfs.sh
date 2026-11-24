#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# mk-initramfs.sh - build the lab initramfs for a QEMU target.
#
#   env/scripts/mk-initramfs.sh x86_64|arm64
#
# Contents: static BusyBox, env/rootfs/{init,motd,profile}, and the in-tree
# kernel modules of the lab kernel (stripped), so that modprobe works for
# e.g. uio_pci_generic (Lab 12) or usbhid (Lab 22).
# Result: $LFD_IMAGES/<target>/initramfs.cpio.gz
set -euo pipefail
. "$(dirname "$0")/common.sh"

target=${1:?usage: mk-initramfs.sh <x86_64|arm64>}
lfd_target "$target"
img=$LFD_IMAGES/$target
mkdir -p "$img"

bb=$img/busybox
if [ ! -x "$bb" ]; then
	if [ "$target" = x86_64 ] && file -L /bin/busybox 2>/dev/null | grep -q 'statically linked'; then
		bb=/bin/busybox
	else
		die "no static busybox for $target: run env/scripts/build-busybox.sh $target"
	fi
fi
[ -f "$LFD_KOUT/Module.symvers" ] || die "build the kernel first: env/scripts/build-kernel.sh $target"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage"/{bin,sbin,etc,proc,sys,dev,tmp,lab,root,usr/bin,usr/sbin,lib/firmware}
cp "$bb" "$stage/bin/busybox"
install -m 0755 "$LFD_REPO/env/rootfs/init" "$stage/init"
cp "$LFD_REPO/env/rootfs/motd" "$stage/etc/motd"
cp "$LFD_REPO/env/rootfs/profile" "$stage/etc/profile"
echo "root:x:0:0:root:/root:/bin/sh" > "$stage/etc/passwd"
echo "root:x:0:" > "$stage/etc/group"

make -s -C "$LFD_KSRC" O="$LFD_KOUT" ARCH="$LFD_ARCH" CROSS_COMPILE="$LFD_CROSS" \
	INSTALL_MOD_PATH="$stage" INSTALL_MOD_STRIP=1 modules_install >/dev/null

(cd "$stage" && find . | LC_ALL=C sort | cpio -o -H newc --quiet --owner 0:0) |
	gzip -9 > "$img/initramfs.cpio.gz"
info "$img/initramfs.cpio.gz ($(du -h "$img/initramfs.cpio.gz" | cut -f1))"
