// SPDX-License-Identifier: GPL-2.0-only
/*
 * modem-boot.c - wayne (Mi 6X / sdm660) modem boot control
 *
 * Target kernel: DevHamid/LKGeek_sdm660 @ fix-ksu-sugrant (4.19)
 *
 * Why subsystem_get() is the entire boot path on this branch:
 *   drivers/soc/qcom/pil-q6v5-mss.c pil_subsys_init() registers subsys name
 *   "modem" with .powerup = modem_powerup(), and modem_powerup() ends in
 *
 *       return pil_boot(&drv->q6->desc);
 *
 *   so subsystem_get("modem") runs the MBA + modem boot end to end. The whole
 *   PIL stack and subsystem_restart are built-in (=y) on this branch.
 *
 *   Inside pil_boot() the MBA image is loaded by pil-msa.c:656-670, which
 *   requests "mba.mbn" (or "mba.b00" for non-elf images). The DT node
 *   carries qcom,sequential-fw-load, so the modem image follows as
 *   modem.mdt + modem.bNN (peripheral-loader.c "%s.mdt" / "%s.b%02d").
 *   mba.mbn is requested under a fixed name - fwname below does not
 *   affect it; wire /lib/firmware/mba.mbn separately (see modem/rmtfs).
 *
 *   Do NOT call pil_mss_reset_load_mba() from this module: pil-q6v5-mss.c
 *   exports nothing (insmod would die with "Unknown symbol") and that
 *   function is the ramdump path only (pil-q6v5-mss.c:198, inside
 *   modem_ramdump()). An earlier version of this file did exactly that and
 *   could never load.
 *
 * sysfs: /sys/class/modem-boot/modem0/
 *   state    ro : "online" / "offline (last err %d)". Diagnostics:
 *                 -22 = PIL rejected the firmware (missing/wrong name),
 *                 -6/-12 = MBA load path, "offline" with no err after a
 *                 boot write = err-ready timeout, usually rmtfs not
 *                 answering.
 *   boot     wo : write "1" -> subsystem_get("modem"). Idempotent; an
 *                 already-online subsys just takes a refcount.
 *   restart  wo : write "1" -> subsystem_restart("modem") - SSR recovery
 *                 ONLY. __subsystem_restart_dev() requires SUBSYS_ONLINE
 *                 and WARNs otherwise, so this is NOT a cold-boot
 *                 substitute; use boot for that.
 *   fwname   rw : firmware basename, honored before the FIRST boot only.
 *                 Default "modem" -> loader requests modem.mdt + modem.bNN
 *                 (peripheral-loader.c: "%s.mdt" and "%s.b%02d").
 *                 Write "MODEM" (no extension) if the firmware partition
 *                 keeps Android's uppercase names.
 *
 * Deliberately does NOT boot the modem at module init: rmtfs must be
 * answering before the modem boots (order: mount -> rmtfs -> boot).
 * A failed boot write never panics the box; the error lands in state.
 */

#include <linux/module.h>
#include <linux/kernel.h>	/* kstrtouint(); this tree has no linux/kstrto.h */
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/sysfs.h>
/* 4.19 sdm660 layout: the header lives at include/soc/qcom/, not
 * include/linux/soc/qcom/. Both of the paths this file used to include
 * (linux/kstrto.h, linux/soc/qcom/subsystem_restart.h) do not exist in
 * LKGeek_sdm660, so the module failed at include time and never built. */
#include <soc/qcom/subsystem_restart.h>

#define MODEM_SUBSYS	"modem"
#define CLASS_NAME	"modem-boot"
#define DEV_NAME	"modem0"

static struct class *modem_boot_class;
static struct device *modem_boot_dev;
static void *modem_handle;		/* subsystem_get() handle, NULL until up */
static char modem_fwname[32] = "modem";
static int last_err;

static bool modem_online(void)
{
	return modem_handle && !IS_ERR_OR_NULL(modem_handle);
}

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	if (modem_online())
		return scnprintf(buf, PAGE_SIZE, "online\n");
	if (last_err)
		return scnprintf(buf, PAGE_SIZE, "offline (last err %d)\n",
				 last_err);
	return scnprintf(buf, PAGE_SIZE, "offline\n");
}

static ssize_t boot_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;
	if (val != 1)
		return -EINVAL;

	if (modem_online())
		return count;		/* already up: refcount taken, no-op */

	if (strcmp(modem_fwname, "modem") != 0) {
		ret = subsystem_set_fwname(MODEM_SUBSYS, modem_fwname);
		if (ret) {
			last_err = ret;
			dev_err(dev, "subsystem_set_fwname(%s) failed: %d\n",
				modem_fwname, ret);
			return ret;
		}
	}

	modem_handle = subsystem_get(MODEM_SUBSYS);
	if (IS_ERR_OR_NULL(modem_handle)) {
		last_err = modem_handle ? PTR_ERR(modem_handle) : -ENODEV;
		modem_handle = NULL;
		dev_err(dev, "subsystem_get(%s) failed: %d\n", MODEM_SUBSYS,
			last_err);
		return last_err;
	}

	last_err = 0;
	dev_info(dev, "modem subsystem online (fw \"%s\")\n", modem_fwname);
	return count;
}

static ssize_t restart_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;
	if (val != 1)
		return -EINVAL;

	ret = subsystem_restart(MODEM_SUBSYS);
	if (ret) {
		last_err = ret;
		dev_err(dev, "subsystem_restart(%s) failed: %d\n", MODEM_SUBSYS,
			ret);
		return ret;
	}
	return count;
}

static ssize_t fwname_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", modem_fwname);
}

static ssize_t fwname_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	size_t len = count;

	/* modem_powerup() copies subsys->fw_name into the PIL descriptor at
	 * powerup time, so this is only meaningful before the first boot. */
	if (modem_online())
		return -EBUSY;
	if (len == 0 || len >= sizeof(modem_fwname))
		return -EINVAL;
	if (buf[len - 1] == '\n')
		len--;
	if (len == 0)
		return -EINVAL;

	memcpy(modem_fwname, buf, len);
	modem_fwname[len] = '\0';
	return count;
}

static DEVICE_ATTR_RO(state);
static DEVICE_ATTR_WO(boot);
static DEVICE_ATTR_WO(restart);
static DEVICE_ATTR_RW(fwname);

static struct attribute *modem_boot_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_boot.attr,
	&dev_attr_restart.attr,
	&dev_attr_fwname.attr,
	NULL,
};

static struct attribute_group modem_boot_group = {
	.attrs = modem_boot_attrs,
};

static int __init modem_boot_init(void)
{
	int ret;

	modem_boot_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(modem_boot_class)) {
		ret = PTR_ERR(modem_boot_class);
		pr_err("modem-boot: class_create failed: %d\n", ret);
		return ret;
	}

	modem_boot_dev = device_create(modem_boot_class, NULL, MKDEV(0, 0),
				       NULL, DEV_NAME);
	if (IS_ERR(modem_boot_dev)) {
		ret = PTR_ERR(modem_boot_dev);
		pr_err("modem-boot: device_create failed: %d\n", ret);
		goto err_class;
	}

	ret = sysfs_create_group(&modem_boot_dev->kobj, &modem_boot_group);
	if (ret) {
		pr_err("modem-boot: sysfs_create_group failed: %d\n", ret);
		goto err_dev;
	}

	dev_info(modem_boot_dev,
		 "modem boot control ready (subsys \"%s\", fw \"%s\")\n",
		 MODEM_SUBSYS, modem_fwname);
	return 0;

err_dev:
	device_unregister(modem_boot_dev);
err_class:
	class_destroy(modem_boot_class);
	return ret;
}

static void __exit modem_boot_exit(void)
{
	if (modem_online())
		subsystem_put(modem_handle);
	if (modem_boot_dev) {
		sysfs_remove_group(&modem_boot_dev->kobj, &modem_boot_group);
		device_unregister(modem_boot_dev);
	}
	if (modem_boot_class)
		class_destroy(modem_boot_class);
}

module_init(modem_boot_init);
module_exit(modem_boot_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("wayne sdm660 modem boot control via subsystem_restart API");
