/*
 * M681 MT6351 vibrator. Register fields and board properties follow the
 * MediaTek 2016 mt6755 vibrator driver (GPL-2.0).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2.
 */
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <mt-plat/upmu_common.h>

struct m681_vibrator {
	struct led_classdev led;
	struct delayed_work stop;
	struct mutex request_lock;
	struct mutex state_lock;
	unsigned int duration_ms;
	bool enabled;
	bool stopping;
};

/* The vendor flag API is unsigned-short, including wrapped negative errno. */
static int vibrator_write(PMU_FLAGS_LIST_ENUM field, unsigned int value)
{
	unsigned short ret = pmic_set_register_value(field, value);

	if (!ret)
		return 0;
	return ret == (unsigned short)-EPERM ? -EPERM : -EIO;
}

static int vibrator_set(struct m681_vibrator *vib, bool enabled)
{
	int ret;

	ret = vibrator_write(MT6351_PMIC_RG_VIBR_EN, enabled);
	if (!ret)
		vib->enabled = enabled;
	return ret;
}

static void vibrator_stop(struct work_struct *work)
{
	struct m681_vibrator *vib = container_of(to_delayed_work(work),
					struct m681_vibrator, stop);
	int ret;

	mutex_lock(&vib->state_lock);
	ret = vibrator_set(vib, false);
	mutex_unlock(&vib->state_lock);
	if (ret)
		dev_err(vib->led.dev, "vibrator disable failed: %d\n", ret);
}

static int vibrator_request(struct m681_vibrator *vib, unsigned int duration)
{
	int ret;

	if (duration > 15000)
		return -ERANGE;
	mutex_lock(&vib->request_lock);
	cancel_delayed_work_sync(&vib->stop);
	mutex_lock(&vib->state_lock);
	if (vib->stopping) {
		ret = -ESHUTDOWN;
	} else {
		ret = vibrator_set(vib, duration != 0);
		if (!ret && duration) {
			vib->duration_ms = duration;
			schedule_delayed_work(&vib->stop, msecs_to_jiffies(duration));
		} else if (ret && vib->enabled) {
			/* A failed replacement must not discard the old stop request. */
			schedule_delayed_work(&vib->stop, 1);
		}
	}
	mutex_unlock(&vib->state_lock);
	mutex_unlock(&vib->request_lock);
	return ret;
}

static struct m681_vibrator *vibrator_from_dev(struct device *dev)
{
	return container_of(dev_get_drvdata(dev), struct m681_vibrator, led);
}

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct m681_vibrator *vib = vibrator_from_dev(dev);
	bool enabled;

	mutex_lock(&vib->state_lock);
	enabled = vib->enabled;
	mutex_unlock(&vib->state_lock);
	return scnprintf(buf, PAGE_SIZE, "%u\n", enabled);
}

static ssize_t activate_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct m681_vibrator *vib = vibrator_from_dev(dev);
	unsigned int value, duration;
	int ret = kstrtouint(buf, 10, &value);

	if (ret)
		return ret;
	if (value > 1)
		return -EINVAL;
	mutex_lock(&vib->state_lock);
	duration = value ? vib->duration_ms : 0;
	mutex_unlock(&vib->state_lock);
	ret = vibrator_request(vib, duration);
	return ret ? ret : count;
}

static ssize_t duration_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	unsigned int duration;
	int ret = kstrtouint(buf, 10, &duration);

	if (!ret)
		ret = vibrator_request(vibrator_from_dev(dev), duration);
	return ret ? ret : count;
}

static DEVICE_ATTR_RO(state);
static DEVICE_ATTR(activate, 0644, state_show, activate_store);
static DEVICE_ATTR_WO(duration);
static struct attribute *vibrator_attrs[] = {
	&dev_attr_state.attr, &dev_attr_activate.attr, &dev_attr_duration.attr, NULL
};
ATTRIBUTE_GROUPS(vibrator);

static int vibrator_brightness(struct led_classdev *led,
			      enum led_brightness brightness)
{
	struct m681_vibrator *vib = container_of(led, struct m681_vibrator, led);
	unsigned int duration;

	mutex_lock(&vib->state_lock);
	duration = brightness ? vib->duration_ms : 0;
	mutex_unlock(&vib->state_lock);
	return vibrator_request(vib, duration);
}

static int m681_vibrator_probe(struct platform_device *pdev)
{
	struct m681_vibrator *vib;
	u32 voltage;
	int ret;

	vib = devm_kzalloc(&pdev->dev, sizeof(*vib), GFP_KERNEL);
	if (!vib)
		return -ENOMEM;
	ret = of_property_read_u32(pdev->dev.of_node, "vib_vol", &voltage);
	if (ret)
		return ret;
	if (voltage > 7)
		return -EINVAL;
	ret = of_property_read_u32(pdev->dev.of_node, "vib_timer", &vib->duration_ms);
	if (ret)
		return ret;
	if (!vib->duration_ms || vib->duration_ms > 15000)
		return -EINVAL;
	mutex_init(&vib->request_lock);
	mutex_init(&vib->state_lock);
	INIT_DELAYED_WORK(&vib->stop, vibrator_stop);
	/* Do not publish a working actuator when the PMIC refuses initialization. */
	ret = vibrator_set(vib, false);
	if (ret)
		return ret;
	ret = vibrator_write(MT6351_PMIC_RG_VIBR_VOSEL, voltage);
	if (ret)
		return ret;
	vib->led.name = "vibrator";
	vib->led.max_brightness = 1;
	vib->led.brightness_set_blocking = vibrator_brightness;
	vib->led.groups = vibrator_groups;
	platform_set_drvdata(pdev, vib);
	return devm_led_classdev_register(&pdev->dev, &vib->led);
}

static void m681_vibrator_shutdown(struct platform_device *pdev)
{
	struct m681_vibrator *vib = platform_get_drvdata(pdev);
	int ret;

	mutex_lock(&vib->request_lock);
	cancel_delayed_work_sync(&vib->stop);
	mutex_lock(&vib->state_lock);
	vib->stopping = true;
	ret = vibrator_set(vib, false);
	mutex_unlock(&vib->state_lock);
	mutex_unlock(&vib->request_lock);
	if (ret)
		dev_err(&pdev->dev, "vibrator shutdown failed: %d\n", ret);
}

static int m681_vibrator_remove(struct platform_device *pdev)
{
	m681_vibrator_shutdown(pdev);
	return 0;
}

static const struct of_device_id m681_vibrator_match[] = {
	{ .compatible = "mediatek,vibrator" }, { }
};
MODULE_DEVICE_TABLE(of, m681_vibrator_match);
static struct platform_driver m681_vibrator_driver = {
	.probe = m681_vibrator_probe,
	.remove = m681_vibrator_remove,
	.shutdown = m681_vibrator_shutdown,
	.driver = {
		.name = "m681-vibrator",
		.of_match_table = m681_vibrator_match,
	},
};
module_platform_driver(m681_vibrator_driver);
MODULE_LICENSE("GPL");
