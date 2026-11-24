// SPDX-License-Identifier: GPL-2.0
/*
 * lfdcal - load and validate a calibration blob with the firmware loader
 * (LFD430 Lab 14).
 *
 * The blob (see tools/mkcal.py) maps raw sensor readings to calibrated
 * values with a piecewise-linear table.  /dev/lfdcal is a misc device; its
 * sysfs directory /sys/class/misc/lfdcal/ has:
 *   fw_name   (RW) firmware file name, default "lfd430/cal.bin"
 *   reload    (WO) "sync", "async" or "user" (sysfs fallback, Deck 05 slide 14)
 *   status    (RO) "state=<defaults|loaded|loading> err=<e> version=<v> entries=<n>"
 *   convert   (RW) write a raw value, read the calibrated value
 * Without a valid blob the driver works with the identity table.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/firmware.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/overflow.h>

#define LFDCAL_MAGIC		0x4344464c	/* "LFDC" */
#define LFDCAL_MAX_ENTRIES	256

struct lfdcal_hdr {
	__le32 magic;
	__le16 version;
	__le16 count;
	__le32 crc32;		/* IEEE CRC-32 of the entries */
	__le32 reserved;
} __packed;

struct lfdcal_entry {
	__le32 raw;
	__le32 cal;
} __packed;

struct cal_table {
	unsigned int n;
	u16 version;
	u32 raw[LFDCAL_MAX_ENTRIES];
	u32 cal[LFDCAL_MAX_ENTRIES];
};

static struct miscdevice lfdcal_misc;
static DEFINE_MUTEX(lock);		/* protects everything below */
static struct cal_table *table;		/* NULL: identity */
static char fw_name[64] = "lfd430/cal.bin";
static int last_err;
static bool loading;
static u32 last_convert;

/*
 * Parse and validate a blob.  Every field comes from a file: trust nothing.
 * Returns a new table or an ERR_PTR().
 */
static struct cal_table *parse_blob(const u8 *data, size_t size)
{
	/*
	 * TODO T2: Validate, in this order, returning -EINVAL for each failure:
	 *    - size >= sizeof(struct lfdcal_hdr)
	 *    - magic == LFDCAL_MAGIC, version == 1, reserved == 0
	 *    - 1 <= count <= LFDCAL_MAX_ENTRIES
	 *    - size == header + count * sizeof(entry), exactly (use
	 *      struct_size()-style overflow-safe arithmetic)
	 *    - crc32 == ~crc32_le(~0, entries, len)  (zlib's CRC-32)
	 *    - raw values strictly increasing
	 *   Then build the table (le32_to_cpu!).  All header fields are
	 *   little-endian: use le16_to_cpu/le32_to_cpu on every read.
	 */
	return ERR_PTR(-ENOSYS);
}

/* Install the result of a load attempt.  Takes the lock. */
static void install(const struct firmware *fw, int err)
{
	struct cal_table *t = NULL, *old = NULL;

	if (!err && fw) {
		t = parse_blob(fw->data, fw->size);
		if (IS_ERR(t)) {
			err = PTR_ERR(t);
			t = NULL;
		}
	} else if (!err) {
		err = -ENOENT;
	}

	mutex_lock(&lock);
	if (t) {
		old = table;
		table = t;
	}
	last_err = err;
	loading = false;
	mutex_unlock(&lock);
	kfree(old);
	if (err)
		dev_warn(lfdcal_misc.this_device, "%s rejected (%d), keeping previous table\n",
			 fw_name, err);
	else
		dev_info(lfdcal_misc.this_device, "%s: %u entries\n", fw_name, t->n);
}

static void fw_callback(const struct firmware *fw, void *ctx)
{
	/*
	 * TODO T4: Asynchronous completion: install the blob (fw may be NULL
	 *   when loading failed), then release_firmware().  Which context
	 *   does this run in, and why can the module not be unloaded while
	 *   a request is pending?
	 */
}

static int reload(const char *how)
{
	const struct firmware *fw;
	char name[sizeof(fw_name)];
	int ret;

	mutex_lock(&lock);
	if (loading) {
		mutex_unlock(&lock);
		return -EBUSY;
	}
	loading = true;
	strscpy(name, fw_name, sizeof(name));
	mutex_unlock(&lock);

	/*
	 * TODO T3: "sync": request_firmware(), install, release_firmware().
	 *   "async": request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
	 *   ..., fw_callback).  "user": the same with FW_ACTION_NOUEVENT,
	 *   which enables the sysfs fallback (/sys/class/firmware/<name>/).
	 *   If the async request cannot even be queued, clear "loading".
	 *   Deck 05 slides 13-14.
	 */
	fw = NULL;
	ret = -ENOSYS;
	mutex_lock(&lock);
	loading = false;
	mutex_unlock(&lock);
	return ret;
}

/* Piecewise-linear interpolation; clamps outside the table. */
static u32 convert(u32 raw)
{
	const struct cal_table *t = table;
	unsigned int i;

	if (!t)
		return raw;
	if (raw <= t->raw[0])
		return t->cal[0];
	for (i = 1; i < t->n; i++) {
		if (raw <= t->raw[i]) {
			u64 span = t->raw[i] - t->raw[i - 1];
			s64 dcal = (s64)t->cal[i] - t->cal[i - 1];

			return t->cal[i - 1] + div64_s64(dcal * (raw - t->raw[i - 1]), span);
		}
	}
	return t->cal[t->n - 1];
}

/* ---------------------------------------------------------------- sysfs */
static ssize_t fw_name_show(struct device *dev, struct device_attribute *a, char *buf)
{
	ssize_t n;

	mutex_lock(&lock);
	n = sysfs_emit(buf, "%s\n", fw_name);
	mutex_unlock(&lock);
	return n;
}

static ssize_t fw_name_store(struct device *dev, struct device_attribute *a,
			     const char *buf, size_t count)
{
	if (!count || count >= sizeof(fw_name) || strstr(buf, ".."))
		return -EINVAL;
	mutex_lock(&lock);
	strscpy(fw_name, buf, sizeof(fw_name));
	strim(fw_name);
	mutex_unlock(&lock);
	return count;
}
static DEVICE_ATTR_RW(fw_name);

static ssize_t reload_store(struct device *dev, struct device_attribute *a,
			    const char *buf, size_t count)
{
	char how[8];
	int ret;

	if (sscanf(buf, "%7s", how) != 1 ||
	    (strcmp(how, "sync") && strcmp(how, "async") && strcmp(how, "user")))
		return -EINVAL;
	ret = reload(how);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(reload);

static ssize_t status_show(struct device *dev, struct device_attribute *a, char *buf)
{
	ssize_t n;

	mutex_lock(&lock);
	n = sysfs_emit(buf, "state=%s err=%d version=%u entries=%u\n",
		       loading ? "loading" : table ? "loaded" : "defaults",
		       last_err, table ? table->version : 0, table ? table->n : 0);
	mutex_unlock(&lock);
	return n;
}
static DEVICE_ATTR_RO(status);

static ssize_t convert_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", READ_ONCE(last_convert));
}

static ssize_t convert_store(struct device *dev, struct device_attribute *a,
			     const char *buf, size_t count)
{
	u32 raw;

	if (kstrtou32(buf, 0, &raw))
		return -EINVAL;
	mutex_lock(&lock);
	WRITE_ONCE(last_convert, convert(raw));
	mutex_unlock(&lock);
	return count;
}
static DEVICE_ATTR_RW(convert);

static struct attribute *lfdcal_attrs[] = {
	&dev_attr_fw_name.attr,
	&dev_attr_reload.attr,
	&dev_attr_status.attr,
	&dev_attr_convert.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lfdcal);

static const struct file_operations lfdcal_fops = {
	.owner = THIS_MODULE,
};

static struct miscdevice lfdcal_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lfdcal",
	.fops = &lfdcal_fops,
	.groups = lfdcal_groups,
};

static int __init lfdcal_init(void)
{
	int ret = misc_register(&lfdcal_misc);

	if (ret)
		return ret;
	/*
	 * TODO T1: Try to load the default blob synchronously at start-up.
	 *   A missing or broken blob is not fatal: keep the defaults.
	 */
	return 0;
}

static void __exit lfdcal_exit(void)
{
	misc_deregister(&lfdcal_misc);
	kfree(table);
}

module_init(lfdcal_init);
module_exit(lfdcal_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Calibration blob loader (LFD430 Lab 14)");
