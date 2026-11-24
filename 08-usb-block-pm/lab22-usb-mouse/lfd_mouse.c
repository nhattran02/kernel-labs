// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_mouse - a USB boot-protocol mouse driver (LFD430 Lab 22).
 *
 * Binds to any HID interface of subclass "boot" and protocol "mouse",
 * switches it to the boot protocol, and turns its 3-4 byte reports
 * (buttons, dx, dy[, wheel]) into input events (/dev/input/eventN).
 *
 * Background: Deck 08 Part 1 (slides 6-15).  The in-tree equivalent is
 * drivers/hid/usbhid/usbmouse.c; the general HID driver (usbhid) normally
 * wins the race for these devices - see the lab book for how to choose.
 */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/slab.h>

#define REPORT_LEN 8

struct lfd_mouse {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct input_dev *input;
	struct urb *urb;
	u8 *buf;			/* kmalloc'ed: DMA-safe, mapped by the USB core */
	char name[128];
	char phys[64];
	atomic_t reports;
};

static void lfd_mouse_irq(struct urb *urb)
{
	struct lfd_mouse *m = urb->context;
	struct input_dev *in = m->input;
	u8 *d = m->buf;
	int ret;

	/*
	 * TODO T3: URB completion (interrupt context).
	 *    - status 0: decode the report: d[0] bits 0/1/2 = left/right/
	 *      middle, d[1] = dx, d[2] = dy (signed!), d[3] = wheel if
	 *      urb->actual_length > 3; report them and input_sync(); count
	 *    - -ECONNRESET / -ENOENT / -ESHUTDOWN: we are being killed or
	 *      the device is gone: return without resubmitting
	 *    - other errors: resubmit (transient)
	 *   Resubmit with GFP_ATOMIC.  Deck 08 slide 13.
	 */
	(void)in; (void)d; (void)ret;
}

/* The URB runs only while someone has the input device open. */
static int lfd_mouse_open(struct input_dev *in)
{
	struct lfd_mouse *m = input_get_drvdata(in);

	/* TODO T4: Start polling: submit the URB (GFP_KERNEL). */
	(void)m;
	return 0;
}

static void lfd_mouse_close(struct input_dev *in)
{
	struct lfd_mouse *m = input_get_drvdata(in);

	/* TODO T4: Stop polling: usb_kill_urb() waits for a running completion. */
	(void)m;
}

static ssize_t reports_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct lfd_mouse *m = usb_get_intfdata(to_usb_interface(dev));

	return sysfs_emit(buf, "%d\n", atomic_read(&m->reports));
}
static DEVICE_ATTR_RO(reports);

static struct attribute *lfd_mouse_attrs[] = {
	&dev_attr_reports.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lfd_mouse);

static int lfd_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep;
	struct lfd_mouse *m;
	int ret;

	/*
	 * TODO T1: Find exactly one interrupt-IN endpoint in the current
	 *   altsetting (usb_find_int_in_endpoint(), -ENODEV otherwise).
	 *   Deck 08 slides 5-7, 10.
	 */
	ep = NULL;

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->udev = udev;
	m->intf = intf;

	/*
	 * TODO T2: Allocate the report buffer (REPORT_LEN, kmalloc) and the
	 *   URB (usb_alloc_urb(0, ...)), and fill it as an interrupt URB on
	 *   usb_rcvintpipe(udev, ep->bEndpointAddress), length min(
	 *   usb_endpoint_maxp(ep), REPORT_LEN), with ep->bInterval.
	 *   Then ask for the boot protocol (HID class request SET_PROTOCOL,
	 *   value 0, to this interface) with usb_control_msg_send().
	 */
	ret = -ENOSYS;
	goto err_free;

	/*
	 * TODO T3: The input device: name from the USB strings (manufacturer,
	 *   product), phys from usb_make_path() + "/input0", id from
	 *   usb_to_input_id(), parent = &intf->dev; capabilities EV_KEY
	 *   BTN_LEFT/RIGHT/MIDDLE and EV_REL REL_X/REL_Y/REL_WHEEL
	 *   (input_set_capability); open/close; input_register_device().
	 *   Deck 08 slide 10.
	 */
	ret = -ENOSYS;
	goto err_free;
	dev_info(&intf->dev, "%s on %s, endpoint %#x every %u ms\n", m->name,
		 m->phys, ep->bEndpointAddress, ep->bInterval);
	return 0;

err_free:
	usb_free_urb(m->urb);
	kfree(m->buf);
	kfree(m);
	return ret;
}

static void lfd_mouse_disconnect(struct usb_interface *intf)
{
	struct lfd_mouse *m = usb_get_intfdata(intf);

	/*
	 * TODO T5: The device is gone (or we are unbound).  Unregistering the
	 *   input device closes it, which kills the URB; only then free
	 *   the URB, the buffer and the state.  Deck 08 slide 11.
	 */
	(void)m;
}

static const struct usb_device_id lfd_mouse_ids[] = {
	{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
			     USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }
};
MODULE_DEVICE_TABLE(usb, lfd_mouse_ids);

static struct usb_driver lfd_mouse_driver = {
	.name = "lfd_mouse",
	.probe = lfd_mouse_probe,
	.disconnect = lfd_mouse_disconnect,
	.id_table = lfd_mouse_ids,
	.dev_groups = lfd_mouse_groups,
};
module_usb_driver(lfd_mouse_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB boot-protocol mouse (LFD430 Lab 22)");
