// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_uart_board - "board file" for Lab 17 on QEMU x86_64 (provided).
 *
 * PCs describe COM ports with ACPI/PnP, not Device Tree.  To keep the lab
 * about the UART itself, this module registers the second PC serial port
 * (COM2: I/O ports 0x2f8-0x2ff, ISA IRQ 3) as a platform device the old way
 * (Deck 05 slide 22), with the same properties a DT node would carry,
 * attached as a software node.  The lab kernel limits the 8250 driver to
 * one port (CONFIG_SERIAL_8250_RUNTIME_UARTS=1), so COM2 is free.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/ioport.h>

static const struct property_entry lfd_uart_props[] = {
	PROPERTY_ENTRY_U32("clock-frequency", 1843200),	/* PC UART crystal */
	PROPERTY_ENTRY_U32("reg-shift", 0),		/* byte-spaced registers */
	{ }
};

static const struct resource lfd_uart_res[] = {
	DEFINE_RES_IO(0x2f8, 8),
	DEFINE_RES_IRQ(3),
};

static const struct platform_device_info lfd_uart_info = {
	.name = "lfd-uart16550",
	.id = PLATFORM_DEVID_NONE,
	.res = lfd_uart_res,
	.num_res = ARRAY_SIZE(lfd_uart_res),
	.properties = lfd_uart_props,
};

static struct platform_device *pdev;

static int __init lfd_uart_board_init(void)
{
	pdev = platform_device_register_full(&lfd_uart_info);
	return PTR_ERR_OR_ZERO(pdev);
}

static void __exit lfd_uart_board_exit(void)
{
	platform_device_unregister(pdev);
}

module_init(lfd_uart_board_init);
module_exit(lfd_uart_board_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("COM2 as a platform device for lfd_uart (LFD430 Lab 17)");
