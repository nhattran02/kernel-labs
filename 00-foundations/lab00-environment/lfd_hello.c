// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_hello - smoke test of the lab environment (LFD430 Lab 00).
 *
 * If you can build this on the host, load it in the guest, read its
 * message with dmesg and unload it, the edit-build-test loop works.
 */
#include <linux/module.h>
#include <linux/utsname.h>

static int __init lfd_hello_init(void)
{
	pr_info("lfd_hello: hello from %s %s\n", utsname()->sysname,
		utsname()->release);
	return 0;
}

static void __exit lfd_hello_exit(void)
{
	pr_info("lfd_hello: goodbye\n");
}

module_init(lfd_hello_init);
module_exit(lfd_hello_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Lab environment smoke test (LFD430 Lab 00)");
