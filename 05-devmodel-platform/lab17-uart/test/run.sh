#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 17 acceptance tests.  QEMU x86_64 (COM2, needs --com2) or the BBB.
LAB=${LAB:-/lab}
. $LAB/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfd_uart_board 2>/dev/null; rmmod lfd_uart 2>/dev/null
dmesg_reset
arch=$(uname -m)
if [ "$arch" = x86_64 ]; then
	check_fail "COM2 (0x2f8) is not claimed by the 8250 driver" grep -q "02f8-02ff : serial" /proc/ioports
fi
check "lfd_uart loads" insmod ./lfd_uart.ko
check_grep "tty driver registered with a dynamic major" "lfd_uart" cat /proc/tty/drivers
if [ "$arch" = x86_64 ]; then
	check "board module creates the COM2 platform device" insmod ./lfd_uart_board.ko
	check_grep "I/O range claimed" "02f8-02ff : lfd-uart16550" cat /proc/ioports
	check_grep "probe message" "16550A at I/O 0x2f8, IRQ 3, 1843200 Hz" dmesg_since
else
	check_grep "probe message" "AM335x at MMIO 0x48022000" dmesg_since
fi
check "/dev/ttyLFD0 exists" test -c /dev/ttyLFD0
run_prog "loopback test" ./test/tty_test /dev/ttyLFD0
check_grep "serial core statistics: tx/rx counted" "tx:[1-9][0-9]* rx:[1-9]" cat /proc/tty/driver/lfd_uart
cat /proc/tty/driver/lfd_uart
# The IRQ is requested in startup() and freed in shutdown(): it exists only
# while the port is open.
sleep 2 < /dev/ttyLFD0 &
sleep 1
check_grep "the IRQ is registered while the port is open" "lfd-uart|48022000.serial" cat /proc/interrupts
wait
check_fail "... and released when it is closed" grep -Eq "lfd-uart|48022000.serial" /proc/interrupts
[ "$arch" = x86_64 ] && check "unload the board module" unload lfd_uart_board
check "unload the driver" unload lfd_uart
check "tty node removed" test ! -e /dev/ttyLFD0
check_clean_log all
summary
