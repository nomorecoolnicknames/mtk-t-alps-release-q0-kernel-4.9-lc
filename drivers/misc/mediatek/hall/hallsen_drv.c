/*
 * BU52013HFV hall sensor, based on the MediaTek vendor driver by
 * liliwen (liliwen@tp-link.com.cn).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2.
 */
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

struct hallsen {
	struct input_dev *input;
	unsigned int gpio;
	int irq;
	bool wake_enabled;
};

/* The legacy driver attributes describe one physical hall sensor. */
static DEFINE_MUTEX(hallsen_lock);
static struct hallsen *hallsen_owner;

static int hallsen_report(struct hallsen *hall)
{
	int state = gpio_get_value_cansleep(hall->gpio);

	if (state < 0)
		return state;
	/* Preserve the vendor HAL's REL_Z={1,2}, REL_Y=3 event format. */
	input_report_rel(hall->input, REL_Z, !!state + 1);
	input_report_rel(hall->input, REL_Y, 3);
	input_sync(hall->input);
	return !!state;
}

static irqreturn_t hallsen_irq_thread(int irq, void *data)
{
	struct hallsen *hall = data;
	int ret = hallsen_report(hall);

	if (ret < 0)
		dev_err_ratelimited(&hall->input->dev, "GPIO read failed: %d\n", ret);
	return IRQ_HANDLED;
}

static ssize_t board_info_show(struct device_driver *driver, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
		"type:\tBU52013HFV\nvendor:\tROHM\ndriver_version:\t1.0.0\n");
}

static ssize_t gpio_state_show(struct device_driver *driver, char *buf)
{
	int ret;

	mutex_lock(&hallsen_lock);
	ret = hallsen_owner ? hallsen_report(hallsen_owner) : -ENODEV;
	mutex_unlock(&hallsen_lock);
	return ret < 0 ? ret : scnprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t devnum_show(struct device_driver *driver, char *buf)
{
	struct input_handle *handle;
	unsigned int number;
	int ret = -ENODEV;

	mutex_lock(&hallsen_lock);
	if (hallsen_owner) {
		rcu_read_lock();
		list_for_each_entry_rcu(handle, &hallsen_owner->input->h_list, d_node) {
			if (handle->name &&
			    sscanf(handle->name, "event%u", &number) == 1) {
				ret = scnprintf(buf, PAGE_SIZE, "%u\n", number);
				break;
			}
		}
		rcu_read_unlock();
	}
	mutex_unlock(&hallsen_lock);
	return ret;
}

static DRIVER_ATTR_RO(board_info);
static DRIVER_ATTR_RO(gpio_state);
static DRIVER_ATTR_RO(devnum);

static struct attribute *hallsen_attrs[] = {
	&driver_attr_board_info.attr,
	&driver_attr_gpio_state.attr,
	&driver_attr_devnum.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hallsen);

static int hallsen_probe(struct platform_device *pdev)
{
	struct device_node *irq_node;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins;
	struct hallsen *hall;
	u32 debounce[2];
	int ret;

	hall = devm_kzalloc(&pdev->dev, sizeof(*hall), GFP_KERNEL);
	if (!hall)
		return -ENOMEM;
	/* Keep the donor's two-node binding; do not fall back to GPIO24. */
	irq_node = of_find_compatible_node(NULL, NULL, "mediatek, hall-eint");
	if (!irq_node)
		return -ENODEV;
	ret = of_property_read_u32_array(irq_node, "debounce", debounce, 2);
	if (!ret)
		hall->irq = of_irq_get(irq_node, 0);
	of_node_put(irq_node);
	if (ret)
		return ret;
	if (hall->irq <= 0)
		return hall->irq ? hall->irq : -EINVAL;
	if (!gpio_is_valid(debounce[0]))
		return -EINVAL;
	hall->gpio = debounce[0];
	pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pinctrl))
		return PTR_ERR(pinctrl);
	pins = pinctrl_lookup_state(pinctrl, "pin_cfg");
	if (IS_ERR(pins))
		return PTR_ERR(pins);
	ret = devm_gpio_request_one(&pdev->dev, hall->gpio, GPIOF_IN, "hall");
	if (ret)
		return ret;
	ret = pinctrl_select_state(pinctrl, pins);
	if (ret)
		return ret;
	ret = gpio_set_debounce(hall->gpio, debounce[1]);
	if (ret)
		return ret;
	hall->input = devm_input_allocate_device(&pdev->dev);
	if (!hall->input)
		return -ENOMEM;
	hall->input->name = "hall_input_device";
	hall->input->id.bustype = BUS_HOST;
	input_set_capability(hall->input, EV_REL, REL_Z);
	input_set_capability(hall->input, EV_REL, REL_Y);
	ret = input_register_device(hall->input);
	if (ret)
		return ret;
	/* Input is ready before IRQ registration can invoke the handler. */
	ret = devm_request_threaded_irq(&pdev->dev, hall->irq, NULL,
		hallsen_irq_thread, IRQF_ONESHOT | IRQF_TRIGGER_RISING |
		IRQF_TRIGGER_FALLING, "hall-eint", hall);
	if (ret)
		return ret;
	mutex_lock(&hallsen_lock);
	if (hallsen_owner) {
		ret = -EBUSY;
	} else {
		hallsen_owner = hall;
		platform_set_drvdata(pdev, hall);
	}
	mutex_unlock(&hallsen_lock);
	if (ret)
		return ret;
	device_init_wakeup(&pdev->dev, true);
	dev_info(&pdev->dev, "BU52013HFV ready: gpio=%u irq=%d\n",
		 hall->gpio, hall->irq);
	return 0;
}

static int hallsen_remove(struct platform_device *pdev)
{
	struct hallsen *hall = platform_get_drvdata(pdev);

	mutex_lock(&hallsen_lock);
	hallsen_owner = NULL;
	mutex_unlock(&hallsen_lock);
	if (hall->wake_enabled)
		disable_irq_wake(hall->irq);
	device_init_wakeup(&pdev->dev, false);
	/* Managed IRQ is released before the managed input and GPIO. */
	return 0;
}

static int hallsen_suspend(struct device *dev)
{
	struct hallsen *hall = dev_get_drvdata(dev);
	int ret;

	if (!device_may_wakeup(dev))
		return 0;
	ret = enable_irq_wake(hall->irq);
	if (!ret)
		hall->wake_enabled = true;
	return ret;
}

static int hallsen_resume(struct device *dev)
{
	struct hallsen *hall = dev_get_drvdata(dev);
	int ret;

	if (!hall->wake_enabled)
		return 0;
	ret = disable_irq_wake(hall->irq);
	if (!ret)
		hall->wake_enabled = false;
	return ret;
}

static SIMPLE_DEV_PM_OPS(hallsen_pm, hallsen_suspend, hallsen_resume);
static const struct of_device_id hallsen_match[] = {
	{ .compatible = "mediatek,hallsen" },
	{ }
};
MODULE_DEVICE_TABLE(of, hallsen_match);

static struct platform_driver hallsen_driver = {
	.probe = hallsen_probe,
	.remove = hallsen_remove,
	.driver = {
		.name = "hall_sensor",
		.of_match_table = hallsen_match,
		.pm = &hallsen_pm,
		.groups = hallsen_groups,
	},
};
module_platform_driver(hallsen_driver);
MODULE_AUTHOR("LiLiwen");
MODULE_DESCRIPTION("MediaTek BU52013HFV hall input driver");
MODULE_LICENSE("GPL");
