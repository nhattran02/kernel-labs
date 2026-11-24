#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# mk-dtb.sh MEM_MIB OUT.dtb - build the board DT for Lab 15:
#  1. ask QEMU for the DT it generates for "virt" (same -m/-smp as run-qemu.sh:
#     the memory and CPU nodes depend on them);
#  2. compile our overlay and apply it with fdtoverlay (from dtc).
set -e
mem=${1:-512}
out=${2:-virt-lfd.dtb}
qemu-system-aarch64 -M virt,dumpdtb=virt.dtb -cpu cortex-a57 -m "$mem" -smp 2 \
	-display none >/dev/null
dtc -I dtb -O dts -q -o virt.dts virt.dtb		# for reading only
dtc -@ -I dts -O dtb -q -o lfd-pl031.dtbo dts/lfd-pl031.dtso
fdtoverlay -i virt.dtb -o "$out" lfd-pl031.dtbo
echo "mk-dtb: $out (QEMU virt DT + dts/lfd-pl031.dtso)"
