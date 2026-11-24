// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_uart - a serial-core driver for 16550-compatible UARTs (Lab 17).
 *
 * One driver, two boards:
 *   - QEMU x86_64: the PC's COM2, a 16550A behind I/O ports 0x2f8-0x2ff,
 *     IRQ 3, 1.8432 MHz clock (platform device from lfd_uart_board.c);
 *   - BeagleBone Black: AM335x UART1, memory-mapped, registers 4 bytes
 *     apart (reg-shift 2), 48 MHz clock, plus the OMAP MDR1 mode register.
 * The register access goes through the iomap API (ioread8/iowrite8), which
 * handles port I/O and MMIO cookies alike (Deck 05 slides 15-18).
 *
 * Devices: /dev/ttyLFD0, ...  Loopback: ioctl(TIOCMBIS, TIOCM_LOOP).
 * References: drivers/tty/serial/liteuart.c (small serial-core driver),
 * drivers/tty/serial/8250/8250_port.c and 8250_omap.c (the full story),
 * Documentation/driver-api/serial/driver.rst, AM335x TRM chapter 19.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/delay.h>
#include <linux/idr.h>
#include <linux/io.h>

#define LFD_UART_NR	2

struct lfd_uart_variant {
	const char *name;
	bool has_mdr1;		/* OMAP: UART disabled until MDR1 = 16x mode */
	bool out2_gates_irq;	/* PC: MCR OUT2 connects the IRQ line */
	unsigned int fifosize;
};

static const struct lfd_uart_variant variant_pc16550 = {
	.name = "16550A", .out2_gates_irq = true, .fifosize = 16,
};

static const struct lfd_uart_variant variant_am335x = {
	.name = "AM335x", .has_mdr1 = true, .fifosize = 64,
};

struct lfd_uart {
	struct uart_port port;
	const struct lfd_uart_variant *v;
	u8 ier;			/* shadow of the interrupt enable register */
};
#define to_lfd_uart(p) container_of(p, struct lfd_uart, port)

static DEFINE_IDA(lfd_uart_ida);
static struct uart_driver lfd_uart_driver;

/* ------------------------------------------------------- register access */
static u8 rd(struct uart_port *port, unsigned int reg)
{
	/*
	 * TODO T1: Read register number reg: ioread8() at membase + (reg <<
	 *   regshift).  Same code for the PC (I/O port cookie) and the BBB.
	 */
	return 0xff;
}

static void wr(struct uart_port *port, unsigned int reg, u8 val)
{
	/* TODO T1: The write counterpart. */
}

/* OMAP: write MDR1 and apply the i202 errata delay (see 8250_omap.c). */
static void wr_mdr1(struct uart_port *port, u8 mode)
{
	wr(port, UART_OMAP_MDR1, mode);
	udelay(2);
	wr(port, UART_FCR, UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR |
	   UART_FCR_CLEAR_XMIT);
}

/* ------------------------------------------------------------ data path */
static void lfd_uart_rx(struct uart_port *port, u8 lsr)
{
	int budget = 256;
	u8 ch, flag;

	/*
	 * TODO T3: While LSR says data ready (UART_LSR_DR) and budget lasts:
	 *   read UART_RX, count it in port->icount.rx, translate errors
	 *   (UART_LSR_PE -> TTY_PARITY, UART_LSR_FE -> TTY_FRAME, count them
	 *   in icount), and hand the byte to the tty layer with
	 *   uart_insert_char(port, lsr, UART_LSR_OE, ch, flag).  Re-read LSR
	 *   for every byte.  Finally tty_flip_buffer_push().
	 */
	(void)budget; (void)ch; (void)flag; (void)lsr;
}

static void lfd_uart_tx(struct uart_port *port)
{
	struct lfd_uart *up = to_lfd_uart(port);
	u8 ch;

	/*
	 * TODO T3: THR empty means the whole TX FIFO is free: send up to
	 *   fifosize bytes with uart_port_tx_limited().  It takes bytes from
	 *   the tty's transmit kfifo, handles x_char, wakes writers and
	 *   calls stop_tx() when nothing is left.
	 */
	(void)up; (void)ch;
}

static irqreturn_t lfd_uart_irq(int irq, void *data)
{
	struct uart_port *port = data;
	struct lfd_uart *up = to_lfd_uart(port);
	unsigned long flags;
	int handled = 0, loops = 32;
	u8 iir, lsr;

	/*
	 * TODO T3: Under the port lock (uart_port_lock_irqsave): loop while
	 *   IIR says an interrupt is pending (!(iir & UART_IIR_NO_INT)),
	 *   at most `loops` times.  Read LSR once per pass; receive if
	 *   UART_LSR_DR, transmit if UART_LSR_THRE and we asked for it
	 *   (UART_IER_THRI in up->ier).  Return IRQ_RETVAL(handled).
	 */
	(void)up; (void)flags; (void)loops; (void)iir; (void)lsr;
	(void)lfd_uart_rx; (void)lfd_uart_tx;
	return IRQ_RETVAL(handled);
}

/* ------------------------------------------------------------ uart_ops */
static unsigned int lfd_uart_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	u8 lsr;

	uart_port_lock_irqsave(port, &flags);
	lsr = rd(port, UART_LSR);
	uart_port_unlock_irqrestore(port, flags);
	return (lsr & UART_LSR_TEMT) ? TIOCSER_TEMT : 0;
}

static void lfd_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct lfd_uart *up = to_lfd_uart(port);
	u8 mcr = 0;

	/*
	 * TODO T4: Translate the TIOCM_* bits (RTS, DTR, OUT1, OUT2, LOOP) to
	 *   UART_MCR_* bits.  On the PC, OUT2 must always be set or the
	 *   UART's interrupt never reaches the interrupt controller.
	 */
	(void)up; (void)mctrl;
	wr(port, UART_MCR, mcr);
}

static unsigned int lfd_uart_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;	/* no modem lines wired */
}

static void lfd_uart_start_tx(struct uart_port *port)
{
	struct lfd_uart *up = to_lfd_uart(port);

	/*
	 * TODO T3: Enable the THR-empty interrupt (if not already).  On a
	 *   16550 this fires at once when THR is empty: the IRQ handler
	 *   then does the actual transmission.  Called with the port lock.
	 */
}

static void lfd_uart_stop_tx(struct uart_port *port)
{
	struct lfd_uart *up = to_lfd_uart(port);

	up->ier &= ~UART_IER_THRI;
	wr(port, UART_IER, up->ier);
}

static void lfd_uart_stop_rx(struct uart_port *port)
{
	struct lfd_uart *up = to_lfd_uart(port);

	up->ier &= ~(UART_IER_RDI | UART_IER_RLSI);
	wr(port, UART_IER, up->ier);
}

static void lfd_uart_break_ctl(struct uart_port *port, int on)
{
	unsigned long flags;
	u8 lcr;

	uart_port_lock_irqsave(port, &flags);
	lcr = rd(port, UART_LCR);
	wr(port, UART_LCR, on ? lcr | UART_LCR_SBC : lcr & ~UART_LCR_SBC);
	uart_port_unlock_irqrestore(port, flags);
}

static int lfd_uart_startup(struct uart_port *port)
{
	struct lfd_uart *up = to_lfd_uart(port);
	unsigned long flags;
	int ret;

	/*
	 * TODO T2: Bring the UART into a known state and enable reception:
	 *    - OMAP only: MDR1 = UART_OMAP_MDR1_DISABLE while configuring
	 *    - FIFOs on and cleared (UART_FCR)
	 *    - read LSR, RX, IIR, MSR once to clear stale conditions
	 *    - request_irq(port->irq, lfd_uart_irq, 0, name, port)
	 *    - 8N1 in LCR, then under the lock: modem control from
	 *      port->mctrl, IER = RDI | RLSI
	 *    - OMAP only: MDR1 = UART_OMAP_MDR1_16X_MODE (wr_mdr1)
	 *   Why request the IRQ here and not in probe()?
	 */
	(void)up; (void)flags; (void)ret;
	return -ENOSYS;
}

static void lfd_uart_shutdown(struct uart_port *port)
{
	struct lfd_uart *up = to_lfd_uart(port);
	unsigned long flags;

	/*
	 * TODO T2: Disable all UART interrupts (IER = 0) under the lock, free
	 *   the IRQ, stop any break, clear the FIFOs.
	 */
	(void)up; (void)flags;
}

static void lfd_uart_set_termios(struct uart_port *port, struct ktermios *new,
				 const struct ktermios *old)
{
	struct lfd_uart *up = to_lfd_uart(port);
	unsigned int baud, quot;
	unsigned long flags;
	u8 lcr;

	/*
	 * TODO T4: Line settings.
	 *    baud = uart_get_baud_rate(port, new, old, 0, uartclk / 16)
	 *    quot = uart_get_divisor(port, baud)   (uartclk / (16 * baud))
	 *    LCR: UART_LCR_WLEN(tty_get_char_size(c_cflag)), CSTOPB ->
	 *         UART_LCR_STOP, PARENB -> UART_LCR_PARITY, !PARODD ->
	 *         UART_LCR_EPAR
	 *    Under the lock: uart_update_timeout(); OMAP: MDR1 disable;
	 *    LCR | DLAB, DLL = quot & 0xff, DLM = quot >> 8, LCR; OMAP: MDR1
	 *    16x.  Report the real rate: tty_termios_encode_baud_rate().
	 */
	(void)up; (void)baud; (void)quot; (void)flags; (void)lcr;
}

static const char *lfd_uart_type(struct uart_port *port)
{
	return to_lfd_uart(port)->v->name;
}

static void lfd_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_16550A;
}

static int lfd_uart_request_port(struct uart_port *port)
{
	return 0;			/* resources are claimed in probe() */
}

static void lfd_uart_release_port(struct uart_port *port)
{
}

static int lfd_uart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return ser->type == PORT_16550A || ser->type == PORT_UNKNOWN ? 0 : -EINVAL;
}

static const struct uart_ops lfd_uart_ops = {
	.tx_empty	= lfd_uart_tx_empty,
	.set_mctrl	= lfd_uart_set_mctrl,
	.get_mctrl	= lfd_uart_get_mctrl,
	.stop_tx	= lfd_uart_stop_tx,
	.start_tx	= lfd_uart_start_tx,
	.stop_rx	= lfd_uart_stop_rx,
	.break_ctl	= lfd_uart_break_ctl,
	.startup	= lfd_uart_startup,
	.shutdown	= lfd_uart_shutdown,
	.set_termios	= lfd_uart_set_termios,
	.type		= lfd_uart_type,
	.config_port	= lfd_uart_config_port,
	.request_port	= lfd_uart_request_port,
	.release_port	= lfd_uart_release_port,
	.verify_port	= lfd_uart_verify_port,
};

/* --------------------------------------------------------------- probe */
static int lfd_uart_map(struct platform_device *pdev, struct uart_port *port)
{
	struct device *dev = &pdev->dev;
	struct resource *res;

	/*
	 * TODO T5: Memory-mapped UART (BBB): devm_platform_get_and_ioremap_
	 *   resource(); iotype UPIO_MEM, mapbase = res->start.  Port I/O
	 *   UART (PC): claim the range with devm_request_region() (-EBUSY
	 *   if taken), then devm_ioport_map(); iotype UPIO_PORT, iobase.
	 *   Either way port->membase gets an iomap cookie.
	 */
	(void)dev; (void)res;
	return -ENOSYS;
}

static int lfd_uart_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct platform_device_id *id;
	struct lfd_uart *up;
	struct uart_port *port;
	u32 shift = 0;
	int ret;

	up = devm_kzalloc(dev, sizeof(*up), GFP_KERNEL);
	if (!up)
		return -ENOMEM;
	port = &up->port;

	/* DT match data, or the platform_device_id table for the PC. */
	up->v = device_get_match_data(dev);
	if (!up->v) {
		id = platform_get_device_id(pdev);
		up->v = id ? (const void *)id->driver_data : NULL;
	}
	if (!up->v)
		return -ENODEV;

	ret = lfd_uart_map(pdev, port);
	if (ret)
		return dev_err_probe(dev, ret, "cannot map registers\n");
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	port->irq = ret;

	/*
	 * TODO T5: Unified device properties (DT or software node):
	 *   "clock-frequency" (required) -> port->uartclk, "reg-shift"
	 *   (optional, default 0) -> port->regshift.  Deck 05 slide 25.
	 */
	(void)shift;

	/*
	 * On the AM335x the UART module is clocked only while a runtime-PM
	 * reference is held (its ti,sysc parent handles the clocks).  On the PC
	 * this is a no-op.  Touching an unclocked OMAP module aborts.
	 */
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;
	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	/*
	 * TODO T5: Is there a UART here?  Write 0x5a to the scratch register
	 *   (UART_SCR), read it back; -ENODEV (and drop the runtime-PM
	 *   reference) if it does not stick.
	 */

	ret = ida_alloc_max(&lfd_uart_ida, LFD_UART_NR - 1, GFP_KERNEL);
	if (ret < 0)
		goto err_pm;
	port->line = ret;
	port->dev = dev;
	port->ops = &lfd_uart_ops;
	port->fifosize = up->v->fifosize;
	port->flags = UPF_BOOT_AUTOCONF;
	port->type = PORT_UNKNOWN;
	spin_lock_init(&port->lock);
	platform_set_drvdata(pdev, up);

	ret = uart_add_one_port(&lfd_uart_driver, port);
	if (ret)
		goto err_ida;
	dev_info(dev, "%s at %s %#llx, IRQ %d, %u Hz, regshift %u -> ttyLFD%d\n",
		 up->v->name, port->iotype == UPIO_PORT ? "I/O" : "MMIO",
		 port->iotype == UPIO_PORT ? (u64)port->iobase : (u64)port->mapbase,
		 port->irq, port->uartclk, port->regshift, port->line);
	return 0;

err_ida:
	ida_free(&lfd_uart_ida, port->line);
err_pm:
	pm_runtime_put_sync(dev);
	return ret;
}

static void lfd_uart_remove(struct platform_device *pdev)
{
	struct lfd_uart *up = platform_get_drvdata(pdev);

	uart_remove_one_port(&lfd_uart_driver, &up->port);
	ida_free(&lfd_uart_ida, up->port.line);
	pm_runtime_put_sync(&pdev->dev);
}

static const struct of_device_id lfd_uart_of_match[] = {
	{ .compatible = "lfd430,am335x-uart", .data = &variant_am335x },
	{ }
};
MODULE_DEVICE_TABLE(of, lfd_uart_of_match);

static const struct platform_device_id lfd_uart_ids[] = {
	{ "lfd-uart16550", (kernel_ulong_t)&variant_pc16550 },
	{ }
};
MODULE_DEVICE_TABLE(platform, lfd_uart_ids);

static struct platform_driver lfd_uart_platform_driver = {
	.probe = lfd_uart_probe,
	.remove = lfd_uart_remove,
	.id_table = lfd_uart_ids,
	.driver = {
		.name = "lfd-uart",
		.of_match_table = lfd_uart_of_match,
	},
};

static struct uart_driver lfd_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "lfd_uart",
	.dev_name = "ttyLFD",
	.nr = LFD_UART_NR,
};

static int __init lfd_uart_init(void)
{
	int ret;

	/*
	 * TODO T6: Register the uart_driver first (it creates the tty driver
	 *   and its dynamic major), then the platform driver; undo the
	 *   first if the second fails.  Exit in the reverse order.
	 */
	(void)ret;
	return -ENOSYS;
}

static void __exit lfd_uart_exit(void)
{
}

module_init(lfd_uart_init);
module_exit(lfd_uart_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("16550/AM335x serial-core driver (LFD430 Lab 17)");
