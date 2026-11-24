// SPDX-License-Identifier: GPL-2.0
/*
 * lfdregs - a register file controlled through ioctl() (LFD430 Lab 05).
 *
 * /dev/lfdregs is a misc device.  The ABI is in include/uapi/lfdregs.h.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/capability.h>
#include <linux/nospec.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>

#include <uapi/lfdregs.h>

static u32 regs[LFDREGS_NREGS];
static DEFINE_MUTEX(regs_lock);

static void regs_reset(void)
{
	memset(regs, 0, sizeof(regs));
	regs[0] = LFDREGS_ID;
}

static long do_get_info(void __user *uarg)
{
	struct lfdregs_info info = {
		.abi_version = LFDREGS_ABI_VERSION,
		.nregs = LFDREGS_NREGS,
	};

	return copy_to_user(uarg, &info, sizeof(info)) ? -EFAULT : 0;
}

static long do_read(void __user *uarg)
{
	/*
	 * TODO T3: Copy a struct lfdregs_reg in, validate index (-EINVAL),
	 *   read the register under the lock, copy the struct back.
	 *   The index comes from user space and selects an array element:
	 *   also clamp it with array_index_nospec() (Spectre v1, see
	 *   Documentation/process/speculation.rst).
	 */
	return -ENOTTY;
}

static long do_write(void __user *uarg)
{
	/*
	 * TODO T3: Like do_read(), but store the value.  Register 0 (the ID)
	 *   is read-only: -EPERM.
	 */
	return -ENOTTY;
}

static long do_bulk_read(void __user *uarg)
{
	/*
	 * TODO T4: Validate first/count against LFDREGS_NREGS without integer
	 *   overflow (first + count can wrap!), then copy count registers to
	 *   u64_to_user_ptr(b.buf).  Copy a snapshot: never call
	 *   copy_to_user() while holding a lock you also take elsewhere
	 *   without thinking about page faults.  count == 0 is a no-op.
	 */
	return -ENOTTY;
}

static long lfdregs_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;

	/*
	 * TODO T1: Dispatch on cmd.  Unknown commands (including other drivers'
	 *   magic numbers) must return -ENOTTY, not -EINVAL.  RESET requires
	 *   CAP_SYS_ADMIN (-EPERM otherwise).  Deck 02 slide 36.
	 */
	return -ENOTTY;
}

static const struct file_operations lfdregs_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = lfdregs_ioctl,
	/*
	 * TODO T5: 32-bit processes on a 64-bit kernel call .compat_ioctl.
	 *   Our arguments are pointers to layout-identical structs: which
	 *   generic helper fits?  Deck 02 slide 37.
	 */
};

static struct miscdevice lfdregs_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lfdregs",
	.fops = &lfdregs_fops,
	.mode = 0666,
};

static int __init lfdregs_init(void)
{
	regs_reset();
	return misc_register(&lfdregs_misc);
}

static void __exit lfdregs_exit(void)
{
	misc_deregister(&lfdregs_misc);
}

module_init(lfdregs_init);
module_exit(lfdregs_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Register file with an ioctl ABI (LFD430 Lab 05)");
