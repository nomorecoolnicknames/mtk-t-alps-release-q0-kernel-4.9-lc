#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

# m681-49-disp: mach/mt_thermal.h (pulled by mt_gpufreq.c) includes "mt_gpufreq.h" from here
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/spm_v2/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/cpuidle_v1/

ccflags-y += -I$(srctree)/drivers/misc/mediatek/irq/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/dramc/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/thermal/fakeHeader/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/$(MTK_PLATFORM)/videox/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/smi/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/mmp/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/lcm/inc/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/cmdq/v2/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/m4u/$(MTK_PLATFORM)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/ext_disp/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/m4u/2.0
ccflags-y += -I$(srctree)/drivers/misc/mediatek/ext_disp/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/cmdq/v2/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/m4u/$(MTK_PLATFORM)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/ext_disp/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/m4u/2.0
ccflags-y += -I$(srctree)/drivers/misc/mediatek/ext_disp/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/$(MTK_PLATFORM)/dispsys
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/
# m681 DVFS stage-3: mt_thermal.h (included by mt_cpufreq.c) pulls
# mtk_thermal_typedefs.h by bare name; give it the mt-plat root.
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/

obj-y += mt_pm_init.o
# m681 WiFi/BT (2026-07-17): clkbuf UN-DEFERRED. clk_buf_init() at late_initcall
# (mt_pm_init.c) is the stock owner of the MT6351 DCXO XO_WCN 26MHz buffer
# (CW00=0x4DFD) + the pwrap DCXO_CONN srclkena engine — the consys clock the
# CONN domain needs before its first AXI access. File is MT6351-true via the
# TU-local #undef (cb0e1d9d). The spm_v2/mt_spm_boot_stubs.c clk_buf_* stubs
# are removed in the same change (they were EXPORT_SYMBOL, not __weak).
# 49-skeleton defer: needs spm_v2/mt_spm_reg_mt6755.h (SPM_MDBSI_CON et al);
# re-enable at the connectivity phase with the spm_v2 include set.
#obj-y += mt_clkbuf_ctl.o
# obj-y += mt_dcm.o  # m681: defer dynamic-clock-mgmt (needs dramc)
#obj-y += mt_spm.o
#obj-y += mt_spm_fs.o
#obj-y += mt_spm_internal.o
obj-y += mt_spm_mtcmos.o
#obj-y += mt_pbm.o
#obj-$(CONFIG_DEBUG_FS) += mt_clkdbg.o
# obj-y += mt_picachu.o  # m681: defer DVFS/EEM/PTP for boot
# 49-skeleton defer (DVFS/leakage stack — pulls thermal/freqhopping deps;
# re-enable one unit at a time after boot, same order as the 4.4 lane):
#obj-y += mt_static_power.o  # m681 p44-1: real leakage model (mt_gpufreq links mt_spower_*; 3.18 parity)
# m681-49-disp: built with the GPU below (mt_gpufreq is its only user).
obj-$(CONFIG_MTK_GPU_SUPPORT) += mt_static_power.o
# m681 cpufreq lane: read-only MT6311 ext-buck presence probe. Default OFF,
# runs only when forge_extbuck_probe is written. Resolves the VPROC-actuator
# question that gates every CPU-DVFS decision (see the file header).
#obj-y += mt_extbuck_probe.o
# m681 DVFS phase-0 (2026-08-14, §11.123 п.5): raw ARMPLL freq knob, default
# inert; ports only the switch recipe, NOT mt_cpufreq (which drags EEM/PTP).
#obj-y += mt_dvfs_p0.o
#obj-y += mt_cpufreq.o  # m681 DVFS stage-3: freq-only port (forge_mt_cpufreq gate)
# obj-y += mt_cpufreq_hybrid.o  # m681: defer DVFS/EEM/PTP for boot
# m681 p44-1 (318->44 parity, D1 in PORT_318_DELTA_AUDIT.md): the bringup stub
# left mfg_sel/MMPLL at LK state and let kbase tear the chain down every idle;
# 3.18 (boots to UI) runs the real mt_gpufreq. Its PMIC writes go through the
# same address-based pmic_config_interface (pmic/mt6353/pmic.o, chip-agnostic,
# MT6351_* addresses supplied by mt_gpufreq.c itself) as on 3.18.
#obj-y += mt_gpufreq.o  # m681 p44-1: real gpufreq (was mt_gpufreq_bringup.o)
#obj-y += mt_gpufreq_fh_fallback.o  # m681 p44-1: one-shot mt_dfs_mmpll while FHCTL deferred
# m681-49-disp: the real gpufreq comes with the Mali (kbase r12p1 links
# mt_gpufreq_*). On 4.9 it runs the stock MT6351-VGPU branch (no
# PMIC_CHIP_MT6353 here) over the read-only PMIC: see mt_gpufreq.c "m681 4.9".
obj-$(CONFIG_MTK_GPU_SUPPORT) += mt_gpufreq.o
obj-$(CONFIG_MTK_GPU_SUPPORT) += mt_gpufreq_fh_fallback.o
# obj-y += mt_ptp.o  # m681: defer DVFS/EEM/PTP for boot
obj-$(CONFIG_MTK_MT6750TT) += mt_eem_turbo.o

# obj-y += mt_vcorefs_governor.o  # m681: defer DVFS/EEM/PTP for boot
# obj-y += mt_vcorefs_manager.o  # m681: defer DVFS/EEM/PTP for boot
obj-y += mt_vcorefs_stub.o  # m681: no-op is_vcorefs_can_work/request_dvfs_opp while vcorefs deferred (msdc autok_dvfs needs them)
obj-y += mt_pmic_stub.o  # m681: no-op PMIC API while pmic/ driver deferred (CONFIG_MTK_PMIC_NEW_ARCH off); keeps SPM/RTC/etc linkable
# m681-49-disp: MT6351 pmu_flags_table for the read-only pmic_get_register_value()
ifneq ($(CONFIG_MTK_PMIC_NEW_ARCH),y)
obj-$(CONFIG_MTK_PMIC_WRAP_HAL) += mt_pmic_flags_mt6351.o
endif
obj-y += mt_misc_stubs.o  # m681: no-op stubs for deferred-subsystem symbols (aee/cirq/charger/ppm/wdt-dfd/display/rtc/sensors) referenced by kept consumers

ifneq ($(CONFIG_ARM64), y)

obj-y += mt-smp.o
obj-y += hotplug.o
obj-y += mt-headsmp.o
obj-y += mt_hotplug.o

else

obj-$(CONFIG_MTK_PSCI) += mt_psci.o  # m681-49: 4.9-shaped mt-boot cpu ops (body from the m5c mt6735 one)

endif

# obj-y += mt_hotplug_strategy_main.o  # m681: defer hotplug governor for boot
# obj-y += mt_hotplug_strategy_core.o  # m681: defer hotplug governor for boot
# obj-y += mt_hotplug_strategy_procfs.o  # m681: defer hotplug governor for boot
# obj-y += mt_hotplug_strategy_cpu.o  # m681: defer hotplug governor for boot
# obj-y += mt_hotplug_strategy_api.o  # m681: defer hotplug governor for boot
# obj-y += mt_hotplug_strategy_algo.o  # m681: defer hotplug governor for boot

obj-$(CONFIG_MTK_PMIC_WRAP_HAL) += forge_chg_observe.o
