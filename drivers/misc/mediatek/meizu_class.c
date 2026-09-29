// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>

struct class *meizu_class;
EXPORT_SYMBOL_GPL(meizu_class);

static int __init meizu_class_init(void)
{
	struct class *class;

	class = class_create(THIS_MODULE, "meizu");
	if (IS_ERR(class))
		return PTR_ERR(class);
	meizu_class = class;
	return 0;
}
subsys_initcall(meizu_class_init);
