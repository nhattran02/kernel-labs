// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_eeprom - read-only driver for the BeagleBone board-ID EEPROM
 * (LFD430 Lab 25, starting code).
 *
 * The board carries a 24C256 (32 KiB, 16-bit word address) on I2C0 at
 * address 0x50.  Its first bytes hold the board identity written at the
 * factory (the layout U-Boot's board/ti/common/board_detect.c reads):
 *
 *   offset  size  field
 *        0     4  magic 0xEE3355AA (bytes AA 55 33 EE, little-endian)
 *        4     8  board name, e.g. "A335BNLT" (not NUL-terminated)
 *       12     4  version, e.g. "00C0"
 *       16    12  serial number
 *
 * The driver reads the device in two ways - a hand-built two-message
 * i2c_transfer() and regmap-i2c - exposes the identity in sysfs and the
 * whole array as a read-only nvmem device.  It never writes: a corrupt
 * header stops U-Boot from recognising the board.
 */
#include <linux/ctype.h>
#include <linux/i2c.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/unaligned.h>

#define LFD_EE_SIZE	SZ_32K
#define LFD_EE_CHUNK	128		/* bytes per read message, like at24 */
#define LFD_EE_MAGIC	0xEE3355AA

#define LFD_EE_OFF_NAME		4
#define LFD_EE_OFF_VERSION	12
#define LFD_EE_OFF_SERIAL	16
#define LFD_EE_HDR_LEN		28

static bool use_regmap;
module_param(use_regmap, bool, 0444);
MODULE_PARM_DESC(use_regmap, "read through regmap-i2c instead of raw i2c_transfer()");

struct lfd_ee {
	struct i2c_client *client;
	struct regmap *map;
	char name[8 + 1];
	char version[4 + 1];
	char serial[12 + 1];
};

/* ------------------------------------------------------ T2: raw transfer */
/*
 * One combined transaction per chunk: START, addr+W, word address (MSB
 * first), repeated START, addr+R, len bytes, STOP.  Without the repeated
 * START another master could move the EEPROM's address pointer between
 * the two messages.
 */
static int lfd_ee_read_raw(struct lfd_ee *ee, unsigned int off, u8 *buf, size_t len)
{
	/*
	 * TODO T2: read len bytes from word address off, in chunks of at most
	 * LFD_EE_CHUNK bytes.  Each chunk is ONE i2c_transfer() of two
	 * struct i2c_msg: a 2-byte write of the address (MSB first), then an
	 * I2C_M_RD message of n bytes.  i2c_transfer() returns the number of
	 * messages done or a negative errno: anything but 2 is a failure.
	 * Hint: Deck 04 "Threaded interrupt handlers" (I2C may sleep),
	 * include/linux/i2c.h, drivers/misc/eeprom/at24.c at24_regmap_read().
	 */
	return -ENOSYS;
}

/* ------------------------------------------------------ T3: regmap */
static bool lfd_ee_writeable(struct device *dev, unsigned int reg)
{
	return false;			/* read-only by design */
}

static const struct regmap_config lfd_ee_regmap_config = {
	/*
	 * TODO T3: describe the device to regmap: 16-bit register (= word
	 * address), 8-bit values, the last valid address, at most
	 * LFD_EE_CHUNK bytes per raw read, never writeable, no cache.
	 * Hint: include/linux/regmap.h struct regmap_config.
	 */
};

static int lfd_ee_read(struct lfd_ee *ee, unsigned int off, void *buf, size_t len)
{
	if (off >= LFD_EE_SIZE || len > LFD_EE_SIZE - off)
		return -EINVAL;
	/* TODO T3: if use_regmap, read with regmap_raw_read() instead. */
	if (use_regmap)
		return regmap_raw_read(ee->map, off, buf, len);
	return lfd_ee_read_raw(ee, off, buf, len);
}

/* ------------------------------------------------------ T4: identity */
/* Factory strings are fixed-width and unterminated; keep them printable. */
static void lfd_ee_copy_field(char *dst, const u8 *src, size_t n)
{
	/* TODO T4: copy n bytes, replace non-printable ones by '?', terminate. */
	dst[0] = '\0';
}
static int lfd_ee_parse_header(struct lfd_ee *ee)
{
	struct device *dev = &ee->client->dev;
	u8 hdr[LFD_EE_HDR_LEN];
	u32 magic;
	int ret;

	/*
	 * TODO T4: read LFD_EE_HDR_LEN bytes at offset 0; on error return
	 * dev_err_probe(...).  Check the magic with get_unaligned_le32()
	 * (why "unaligned"?), then fill ee->name, ee->version, ee->serial.
	 */
	return -ENOSYS;
}

static ssize_t board_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lfd_ee *ee = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", ee->name);
}
static DEVICE_ATTR_RO(board_name);

static ssize_t board_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lfd_ee *ee = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", ee->version);
}
static DEVICE_ATTR_RO(board_version);

static ssize_t serial_number_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lfd_ee *ee = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", ee->serial);
}
static DEVICE_ATTR_RO(serial_number);

static ssize_t access_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", use_regmap ? "regmap" : "i2c_transfer");
}
static DEVICE_ATTR_RO(access_mode);

static struct attribute *lfd_ee_attrs[] = {
	&dev_attr_board_name.attr,
	&dev_attr_board_version.attr,
	&dev_attr_serial_number.attr,
	&dev_attr_access_mode.attr,
	NULL
};
ATTRIBUTE_GROUPS(lfd_ee);

/* ------------------------------------------------------ T5: nvmem */
static int lfd_ee_nvmem_read(void *priv, unsigned int off, void *val, size_t bytes)
{
	return lfd_ee_read(priv, off, val, bytes);
}

/* ------------------------------------------------------ T1: probe */
static int lfd_ee_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct nvmem_config nc = {};
	struct nvmem_device *nvmem;
	struct lfd_ee *ee;
	int ret;

	/*
	 * TODO T1: refuse adapters that cannot do I2C_FUNC_I2C (combined
	 * messages), then allocate ee with devm_kzalloc() and set ee->client.
	 */
	return -ENOSYS;

	ee->map = devm_regmap_init_i2c(client, &lfd_ee_regmap_config);
	if (IS_ERR(ee->map))
		return dev_err_probe(dev, PTR_ERR(ee->map), "regmap\n");

	ret = lfd_ee_parse_header(ee);
	if (ret)
		return ret;
	/* before nvmem and before the attributes appear (after probe) */
	i2c_set_clientdata(client, ee);

	/*
	 * TODO T5: register a read-only nvmem device of LFD_EE_SIZE bytes,
	 * named "lfd-eeprom" with NVMEM_DEVID_AUTO, type NVMEM_TYPE_EEPROM,
	 * reg_read = lfd_ee_nvmem_read, priv = ee; devm_nvmem_register().
	 * Hint: include/linux/nvmem-provider.h, at24.c at24_probe().
	 */
	(void)nc;
	(void)nvmem;

	dev_info(dev, "%s rev %s serial %s (%s)\n", ee->name, ee->version,
		 ee->serial, use_regmap ? "regmap" : "i2c_transfer");
	return 0;
}

static const struct of_device_id lfd_ee_of_match[] = {
	{ .compatible = "atmel,24c256" },	/* the BBB board-ID EEPROM */
	{ }
};
MODULE_DEVICE_TABLE(of, lfd_ee_of_match);

static const struct i2c_device_id lfd_ee_ids[] = {
	{ "24c256" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lfd_ee_ids);

static struct i2c_driver lfd_ee_driver = {
	.driver = {
		.name = "lfd_eeprom",
		.of_match_table = lfd_ee_of_match,
		.dev_groups = lfd_ee_groups,
	},
	.probe = lfd_ee_probe,
	.id_table = lfd_ee_ids,
};
module_i2c_driver(lfd_ee_driver);

MODULE_DESCRIPTION("Read-only BeagleBone board-ID EEPROM driver (LFD430 Lab 25)");
MODULE_LICENSE("GPL");
