/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/cpu_ops.h>
#include <linux/psci.h>	/* 4.9: asm/psci.h is gone */
#include <mach/mt_spm_mtcmos.h>
#include "mt_cpu_psci_ops.h"

#ifdef CONFIG_SMP

/* FORGE m5c boot markers (defined in arch/arm64/kernel/setup.c) */
extern void forge_kmark(int ms);


/* 4.9 cpu_operations: cpu_init/cpu_init_idle take only the cpu number */
static int __init mt_psci_cpu_init(unsigned int cpu)
{
	return 0;
}

static int __init mt_psci_cpu_prepare(unsigned int cpu)
{
	forge_kmark(23);		/* FORGE: cpu_prepare entered */
	if (cpu == 1)
		spm_mtcmos_cpu_init();
	forge_kmark(24);		/* FORGE: cpu_prepare done (mtcmos init ok) */
	return cpu_psci_ops.cpu_prepare(cpu);
}

static int mt_psci_cpu_boot(unsigned int cpu)
{
	int ret;

	forge_kmark(25);		/* FORGE: cpu_boot entered */
	ret = cpu_psci_ops.cpu_boot(cpu);
	forge_kmark(26);		/* FORGE: psci cpu_on returned */
	if (ret < 0)
		return ret;

	ret = spm_mtcmos_ctrl_cpu(cpu, STA_POWER_ON, 1);
	forge_kmark(27);		/* FORGE: mtcmos POWER_ON returned */
	return ret;
}

#ifdef CONFIG_HOTPLUG_CPU
static int mt_psci_cpu_disable(unsigned int cpu)
{
	return cpu_psci_ops.cpu_disable(cpu);
}

static void mt_psci_cpu_die(unsigned int cpu)
{
	cpu_psci_ops.cpu_die(cpu);
}

static int mt_psci_cpu_kill(unsigned int cpu)
{
	int ret;

	ret = cpu_psci_ops.cpu_kill(cpu);
	if (!ret)
		pr_warn("CPU%d may not have shut down cleanly\n", cpu);

	return !spm_mtcmos_ctrl_cpu(cpu, STA_POWER_DOWN, 1);
}
#endif

#ifdef CONFIG_CPU_IDLE
static int mt_psci_cpu_init_idle(unsigned int cpu)
{
	return cpu_psci_ops.cpu_init_idle(cpu);
}

static int mt_psci_cpu_suspend(unsigned long index)
{
	return cpu_psci_ops.cpu_suspend(index);
}
#endif

const struct cpu_operations mt_cpu_psci_ops = {
	.name = "mt-boot",
#ifdef CONFIG_CPU_IDLE
	.cpu_init_idle	= mt_psci_cpu_init_idle,
	.cpu_suspend	= mt_psci_cpu_suspend,
#endif
	.cpu_init = mt_psci_cpu_init,
	.cpu_prepare = mt_psci_cpu_prepare,
	.cpu_boot = mt_psci_cpu_boot,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable = mt_psci_cpu_disable,
	.cpu_die = mt_psci_cpu_die,
	.cpu_kill = mt_psci_cpu_kill,
#endif
};

#endif
