#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 22 acceptance tests (QEMU: -device qemu-xhci -device usb-mouse; the
# host moves the mouse through the QEMU monitor, see the Makefile).
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfd_mouse 2>/dev/null
dmesg_reset
intf=$(grep -l "^03$" /sys/bus/usb/devices/*:*/bInterfaceClass 2>/dev/null | head -1)
intf=${intf%/bInterfaceClass}
check "a USB HID interface is present (${intf##*/})" test -n "$intf"
check_grep "it is a boot-protocol mouse (protocol 02)" "^02$" cat $intf/bInterfaceProtocol
check_grep "QEMU USB Mouse (vendor 0627)" "^0627$" cat $intf/../idVendor
check "lfd_mouse loads and binds" insmod ./lfd_mouse.ko
check_grep "bound to lfd_mouse" "lfd_mouse$" readlink $intf/driver
check_grep "probe message" "endpoint 0x81 every [0-9]+ ms" dmesg_since
ev=$(ls -d $intf/input/input*/event* 2>/dev/null | head -1)
check "an evdev node exists for it (${ev##*/})" test -c /dev/input/${ev##*/}
run_prog "events from the moving mouse" ./test/input_test /dev/input/${ev##*/} 60
check "reports were counted" sh -c "[ \$(cat $intf/reports) -gt 0 ]"

# Driver competition: let usbhid take the device, then hand it back.
check "usbhid loads" modprobe usbhid
check "unbind from lfd_mouse" sh -c "echo ${intf##*/} > /sys/bus/usb/drivers/lfd_mouse/unbind"
check "bind to usbhid" sh -c "echo ${intf##*/} > /sys/bus/usb/drivers/usbhid/bind"
check_grep "now driven by usbhid" "usbhid$" readlink $intf/driver
check "unbind from usbhid" sh -c "echo ${intf##*/} > /sys/bus/usb/drivers/usbhid/unbind"
check "bind back to lfd_mouse" sh -c "echo ${intf##*/} > /sys/bus/usb/drivers/lfd_mouse/bind"
check_grep "lfd_mouse again" "lfd_mouse$" readlink $intf/driver
check "lfd_mouse unloads" unload lfd_mouse
check "usbhid unloads" unload usbhid
check_clean_log all
summary
