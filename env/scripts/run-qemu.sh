#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# run-qemu.sh - boot the lab kernel in QEMU with the repository shared at /lab.
#
#   env/scripts/run-qemu.sh [x86_64|arm64] [options] [-- extra QEMU args]
#
#   --run PATH       run PATH (relative to the repo root) in the guest, power
#                    off, and exit with the script's status (unattended mode)
#   --edu[=MASK]     add QEMU's "edu" PCI device (optional dma_mask=MASK)
#   --net MODEL      add a user-mode NIC: e1000, rtl8139, virtio-net-pci
#   --tftp DIR       with --net: QEMU's built-in TFTP server serves DIR
#                    (guest: tftp -g -r FILE 10.0.2.2)
#   --usb-mouse      add an xHCI controller with a USB mouse
#   --com2 SPEC      x86_64: second 16550 UART (COM2) backend, e.g. null,
#                    pty, file:/tmp/com2.txt, tcp::4555,server,nowait
#   --dtb FILE       arm64: boot with FILE instead of QEMU's generated DT
#   --append ARGS    extra kernel command-line arguments
#   --hmp CMDS       send monitor commands after boot, ';'-separated;
#                    "sleep N" pauses (e.g. "sleep 20;mouse_move 10 0")
#   --gdb            freeze at start, wait for GDB on localhost:1234
#   --smp N          number of vCPUs (default 2)
#   --mem MIB        guest RAM (default $LFD_MEM)
#   --timeout SEC    unattended mode time limit (default 900)
#
# Interactive mode: Ctrl-A X quits QEMU.  The HMP monitor is also reachable
# from another terminal with env/scripts/hmp.sh "info pci".
set -euo pipefail
. "$(dirname "$0")/common.sh"

target=x86_64
case "${1:-}" in x86_64|arm64) target=$1; shift ;; esac
lfd_target "$target"
img=$LFD_IMAGES/$target

run= edu= net= tftp= usbmouse= com2= dtb= append= hmp= gdb= smp=2 mem=$LFD_MEM tmo=900
while [ $# -gt 0 ]; do
	case "$1" in
	--run) run=$2; shift ;;
	--edu) edu=default ;;
	--edu=*) edu=${1#--edu=} ;;
	--net) net=$2; shift ;;
	--tftp) tftp=$2; shift ;;
	--usb-mouse) usbmouse=1 ;;
	--com2) com2=$2; shift ;;
	--dtb) dtb=$2; shift ;;
	--append) append="$append $2"; shift ;;
	--hmp) hmp=$2; shift ;;
	--gdb) gdb=1 ;;
	--smp) smp=$2; shift ;;
	--mem) mem=$2; shift ;;
	--timeout) tmo=$2; shift ;;
	--) shift; break ;;
	*) die "unknown option $1 (see the header of $0)" ;;
	esac
	shift
done

initrd=$img/initramfs.cpio.gz
[ -f "$initrd" ] || die "missing $initrd: run env/scripts/mk-initramfs.sh $target"
mon=$img/monitor.sock
rm -f "$mon"

args=(-m "$mem" -smp "$smp" -no-reboot -display none
      -monitor "unix:$mon,server,nowait"
      -virtfs "local,path=$LFD_REPO,mount_tag=lab,security_model=none,id=lab"
      -initrd "$initrd")
if [ -n "$run" ]; then
	args+=(-serial stdio)
	# Keep pr_info() noise off the console; dmesg still has everything.
	append="loglevel=5 $append lfd.run=$run"
else
	args+=(-serial mon:stdio)
fi

case $target in
x86_64)
	qemu=qemu-system-x86_64
	args+=(-M q35 -kernel "$LFD_KOUT/arch/x86/boot/bzImage")
	cmdline="console=ttyS0 panic=-1"
	if [ -w /dev/kvm ]; then args+=(-enable-kvm -cpu host); else args+=(-cpu max); fi
	[ -n "$com2" ] && args+=(-serial "$com2")
	;;
arm64)
	qemu=qemu-system-aarch64
	args+=(-M virt -cpu cortex-a57 -kernel "$LFD_KOUT/arch/arm64/boot/Image")
	cmdline="console=ttyAMA0 panic=-1"
	[ -n "$dtb" ] && args+=(-dtb "$dtb")
	;;
esac

if [ -n "$edu" ]; then
	if [ "$edu" = default ]; then args+=(-device edu)
	else args+=(-device "edu,dma_mask=$edu"); fi
fi
if [ -n "$net" ]; then
	args+=(-nic "user,model=$net${tftp:+,tftp=$tftp}")
else
	args+=(-nic none)
fi
[ -n "$usbmouse" ] && args+=(-device qemu-xhci,id=xhci -device usb-mouse,bus=xhci.0)
[ -n "$gdb" ] && { args+=(-s -S); cmdline="$cmdline nokaslr"; info "waiting for gdb on :1234"; }

if [ -n "$hmp" ]; then
	( IFS=';'; for c in $hmp; do
		c=${c#"${c%%[![:space:]]*}"}
		case "$c" in
		sleep\ *) sleep "${c#sleep }" ;;
		*) "$LFD_REPO/env/scripts/hmp.sh" "$c" >/dev/null 2>&1 || true ;;
		esac
	  done ) &
fi

if [ -z "$run" ]; then
	exec "$qemu" "${args[@]}" -append "$cmdline $append" "$@"
fi

log=$img/last-run.log
set +e
timeout "$tmo" "$qemu" "${args[@]}" -append "$cmdline $append" "$@" </dev/null | tee "$log"
set -e
status=$(sed -n 's/^=== lfd: exit status \([0-9]*\).*/\1/p' "$log" | tail -1)
[ -n "$status" ] || { echo "run-qemu: no exit status (crash or timeout); see $log"; exit 2; }
exit "$status"
