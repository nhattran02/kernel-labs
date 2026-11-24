#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# mk-bbb-rootfs.sh - root file system and TFTP files for the BeagleBone Black.
#
#   env/scripts/build-kernel.sh bbb
#   env/scripts/build-busybox.sh bbb
#   env/scripts/mk-bbb-rootfs.sh            # (re)creates $LFD_BBB_ROOTFS
#
# Result:
#   $LFD_BBB_ROOTFS   BusyBox root for NFS: kernel modules, /root/lfd430/common,
#                     init on the serial console (ttyS0, 115200 8N1)
#   $LFD_BBB_TFTP     zImage and am335x-boneblack.dtb for U-Boot's tftp
#
# Lab directories are added later with "make install" in each BBB lab.
# The host side (static IP, NFS export, TFTP server, U-Boot commands) is
# in the lab book, appendix "BeagleBone Black: boot over the network".
set -euo pipefail
. "$(dirname "$0")/common.sh"
lfd_target bbb

root=$LFD_BBB_ROOTFS
tftp=$LFD_BBB_TFTP
bb=$LFD_IMAGES/bbb/busybox
[ -x "$bb" ] || die "no BusyBox for bbb: run env/scripts/build-busybox.sh bbb"
[ -f "$LFD_KOUT/Module.symvers" ] || die "build the kernel first: env/scripts/build-kernel.sh bbb"
case "$root" in /|/home|"$HOME") die "refusing LFD_BBB_ROOTFS=$root" ;; esac

info "root file system in $root"
mkdir -p "$root"
# Keep installed labs (root/lfd430) across rebuilds; recreate the rest.
for d in bin sbin usr etc lib; do rm -rf "${root:?}/$d"; done
mkdir -p "$root"/{bin,sbin,usr/bin,usr/sbin,etc/init.d,proc,sys,dev,tmp,run,mnt,lib/firmware,root/lfd430/common}
cp "$bb" "$root/bin/busybox"
# The other applet links are created by rcS at the first boot.
ln -sf busybox "$root/bin/sh"
ln -sf ../bin/busybox "$root/sbin/init"

cat > "$root/etc/inittab" <<'EOF'
::sysinit:/etc/init.d/rcS
ttyS0::respawn:-/bin/sh
::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a -r
EOF
cat > "$root/etc/init.d/rcS" <<'EOF'
#!/bin/sh
# Applet links, in case they were not created on the host.
[ -e /bin/ls ] || /bin/busybox --install -s
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount | grep -q ' /dev ' || mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts && mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp
mount -t tmpfs tmpfs /run
mount -t debugfs debugfs /sys/kernel/debug
mount -t tracefs tracefs /sys/kernel/tracing 2>/dev/null
hostname bbb-lfd430
ip link set lo up
cat /etc/motd
EOF
chmod 0755 "$root/etc/init.d/rcS"
cp "$LFD_REPO/env/rootfs/profile" "$root/etc/profile"
echo 'export LAB=/root/lfd430' >> "$root/etc/profile"
echo 'cd /root/lfd430 2>/dev/null' >> "$root/etc/profile"
printf 'LFD430 labs - BeagleBone Black (%s)\nLabs: /root/lfd430  (make install on the host)\n\n' \
	"$(cat "$LFD_KOUT/include/config/kernel.release")" > "$root/etc/motd"
echo "root:x:0:0:root:/root:/bin/sh" > "$root/etc/passwd"
echo "root:x:0:" > "$root/etc/group"
cp "$LFD_REPO/common/lib.sh" "$root/root/lfd430/common/"

make -s -C "$LFD_KSRC" O="$LFD_KOUT" ARCH="$LFD_ARCH" CROSS_COMPILE="$LFD_CROSS" \
	INSTALL_MOD_PATH="$root" INSTALL_MOD_STRIP=1 modules_install >/dev/null

info "TFTP files in $tftp"
mkdir -p "$tftp"
cp "$LFD_KOUT/arch/arm/boot/zImage" "$tftp/"
cp "$LFD_KOUT/arch/arm/boot/dts/ti/omap/am335x-boneblack.dtb" "$tftp/"
info "done.  Export $root over NFS (no_root_squash) and serve $tftp over TFTP."
