#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 25 acceptance tests - run ON THE BEAGLEBONE BLACK as root, with the
# stock am335x-boneblack.dtb (the board-ID EEPROM is I2C0 address 0x50).
#
# The in-tree at24 driver is the reference: its nvmem dump is compared byte
# for byte with yours, in both access modes.  Nothing is ever written.
LAB=${LAB:-/root/lfd430}
. $LAB/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
DEV=0-0050
SYS=/sys/bus/i2c/devices/$DEV

rmmod lfd_eeprom 2>/dev/null
modprobe at24 2>/dev/null
# make sure at24 owns the device for the reference dump
[ -e $SYS/driver ] || echo $DEV > /sys/bus/i2c/drivers/at24/bind 2>/dev/null
check "the board-ID EEPROM is instantiated from DT" test -d $SYS
check_grep "at24 is bound to it" "at24" readlink $SYS/driver
REF=$(ls -d $SYS/*/nvmem 2>/dev/null | head -1)
check "at24 exposes an nvmem file" test -n "$REF"
dd if="$REF" of=/tmp/ee.ref bs=4096 count=8 2>/dev/null
check_grep "reference dump is 32768 bytes" "(^|[^0-9])32768$" sh -c "wc -c < /tmp/ee.ref"
check_grep "reference header has the TI magic" "aa 55 33 ee" od -An -tx1 -N4 /tmp/ee.ref

echo $DEV > /sys/bus/i2c/drivers/at24/unbind
dmesg_reset
for mode in 0 1; do
	m=raw; [ $mode = 1 ] && m=regmap
	check "[$m] lfd_eeprom loads" insmod ./lfd_eeprom.ko use_regmap=$mode
	check_grep "[$m] lfd_eeprom is bound to $DEV" "lfd_eeprom" readlink $SYS/driver
	check_grep "[$m] access mode reported" "^$( [ $m = raw ] && echo i2c_transfer || echo regmap)\$" \
		cat $SYS/access_mode
	check_grep "[$m] board name looks like a BeagleBone" "^A335" cat $SYS/board_name
	check_grep "[$m] serial number is printable" "^[[:print:]]+$" cat $SYS/serial_number
	NV=$(ls -d $SYS/lfd-eeprom*/nvmem 2>/dev/null | head -1)
	check "[$m] nvmem device registered" test -n "$NV"
	dd if="$NV" of=/tmp/ee.$m bs=4096 count=8 2>/dev/null
	check "[$m] full 32 KiB dump identical to at24" cmp /tmp/ee.ref /tmp/ee.$m
	dd if="$NV" of=/tmp/ee.odd bs=1 skip=4093 count=7 2>/dev/null
	check "[$m] unaligned read across a chunk boundary" \
		sh -c "dd if=/tmp/ee.ref bs=1 skip=4093 count=7 2>/dev/null | cmp - /tmp/ee.odd"
	check_fail "[$m] writing the nvmem file is refused" sh -c "printf x | dd of=$NV bs=1 count=1 conv=notrunc 2>/dev/null"
	check "[$m] lfd_eeprom unloads" unload lfd_eeprom
done
check "at24 can take the device back" sh -c "echo $DEV > /sys/bus/i2c/drivers/at24/bind"
check_clean_log all
summary
