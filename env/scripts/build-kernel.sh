#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# build-kernel.sh - configure and build the lab kernel for one target.
#
#   env/scripts/build-kernel.sh x86_64          # QEMU q35 (most labs)
#   env/scripts/build-kernel.sh arm64           # QEMU virt (Lab 15)
#   env/scripts/build-kernel.sh bbb             # BeagleBone Black
#   env/scripts/build-kernel.sh x86_64 menuconfig
#
# Every run regenerates $LFD_KBUILD/<target>/.config from a defconfig plus
# the fragments in env/configs/, then checks that every option requested
# by the fragments really ended up in .config.  So env/configs/*.config is
# the source of truth: edit it, rerun, and the change is applied.  The build
# itself stays incremental (Kbuild only recompiles what the change affects).
#
# Changes made with menuconfig are lost on the next run.  Copy them into the
# fragment, or keep the current .config for one run with
#   LFD_KEEP_CONFIG=1 env/scripts/build-kernel.sh x86_64
set -euo pipefail
. "$(dirname "$0")/common.sh"

target=${1:?usage: build-kernel.sh <x86_64|arm64|bbb> [make targets]}
shift
lfd_target "$target"

[ -f "$LFD_KSRC/Makefile" ] || die "LFD_KSRC=$LFD_KSRC is not a kernel tree"
grep -q '^PATCHLEVEL = 12$' "$LFD_KSRC/Makefile" ||
	echo "warning: these labs are written and tested against Linux v6.12"
mkdir -p "$LFD_KOUT"
K=(make -C "$LFD_KSRC" O="$LFD_KOUT" ARCH="$LFD_ARCH" CROSS_COMPILE="$LFD_CROSS")
cfg=$LFD_REPO/env/configs

case $target in
x86_64) base=x86_64_defconfig
	frags=("$LFD_KSRC/kernel/configs/kvm_guest.config" "$cfg/qemu-x86_64.config")
	images=(bzImage modules scripts_gdb) ;;
arm64)	base=allnoconfig
	frags=("$cfg/qemu-arm64.config")
	images=(Image modules scripts_gdb) ;;
bbb)	base=omap2plus_defconfig
	frags=("$cfg/bbb.config")
	images=(zImage modules dtbs) ;;
esac

if [ ! -f "$LFD_KOUT/.config" ] || [ "${LFD_KEEP_CONFIG:-0}" != 1 ]; then
	info "configuring $target: $base + ${frags[*]##*/}"
	"${K[@]}" "$base"
	ARCH=$LFD_ARCH CROSS_COMPILE=$LFD_CROSS \
		"$LFD_KSRC/scripts/kconfig/merge_config.sh" -m -O "$LFD_KOUT" \
		"$LFD_KOUT/.config" "${frags[@]}" >/dev/null
	"${K[@]}" olddefconfig
	# Report requests that Kconfig silently dropped (unmet dependencies).
	bad=0
	while IFS= read -r l; do
		case "$l" in
		CONFIG_*=*) sym=${l%%=*}; want=$l ;;
		"# CONFIG_"*" is not set") sym=${l#\# }; sym=${sym%% *}; want=$l ;;
		*) continue ;;
		esac
		have=$(grep -E "^($sym=|# $sym is not set)" "$LFD_KOUT/.config" || true)
		if [ "$have" != "$want" ] &&
		   ! { [ -z "$have" ] && [ "${want#\# }" != "$want" ]; }; then
			echo "  requested: $want   got: ${have:-<absent>}"
			bad=1
		fi
	done < <(cat "${frags[@]/#$LFD_KSRC*/}" 2>/dev/null)
	[ $bad = 0 ] && info "all fragment options applied" ||
		echo "warning: some options were not applied (see above)"
fi

if [ $# -gt 0 ]; then
	"${K[@]}" "$@"
else
	extra=()
	# Overlays (Labs 16, 17) need __symbols__ in the base DTB.
	[ "$target" = bbb ] && extra=(DTC_FLAGS=-@)
	"${K[@]}" -j"$(nproc)" "${extra[@]}" "${images[@]}"
	info "done: $LFD_KOUT"
fi
