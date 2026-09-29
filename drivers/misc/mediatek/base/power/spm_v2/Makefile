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

ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/spm_v2/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/cpuidle_v2/

ccflags-y += -I$(srctree)/drivers/misc/mediatek/irq/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/dramc/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/thermal/fakeHeader/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic_wrap/$(MTK_PLATFORM)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/power_gs/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/cameraisp/src/$(MTK_PLATFORM)/inc/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/met/$(MTK_PLATFORM)/

ifeq ($(CONFIG_ARCH_MT6797), y)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/scp/$(MTK_PLATFORM)/
endif

#ifeq ($(CONFIG_ARCH_MT6735),y)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/mt6757/videox/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/mt6757/dispsys/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/mt6757/dispsys/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/cmdq/v2/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/cmdq/v2/$(MTK_PLATFORM)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/lcm/inc/
#endif

ccflags-$(CONFIG_MTK_MET) += -DMET_USER_EVENT_SUPPORT

# m681/mt6755: build the graft's validated mt6755 SPM (mt_* files; coexist with
# the donor mtk_* set by name). 3.18->4.4 forward-port; carries the m681 MTCMOS
# with its larb0 / bounded-poll wedge fixes. (4.4 MACH_ convention; was ARCH_MT6755.)
ifeq ($(CONFIG_MACH_MT6755), y)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/cpuidle_v1/
# m681: with the real MT6353 PMIC (NEW_ARCH) on, mt_spm.c pulls pmic_api_buck.h /
# pmic_api_ldo.h (vcore/vsram buck control). Provide the pmic dispatcher + mt6353
# chip-header include paths so they resolve.
ifeq ($(CONFIG_MTK_PMIC_CHIP_MT6353),y)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include/mt6353/
endif
# obj-y += mt_idle.o  # m681: defer runtime-PM (idle/dpidle/sodi/vcorefs) for boot
# obj-y += mt_idle_profile.o  # m681: defer runtime-PM (idle/dpidle/sodi/vcorefs) for boot
# obj-y += mt_idle_mt6755.o  # m681: defer runtime-PM (idle/dpidle/sodi/vcorefs) for boot
obj-y += mt_spm_boot_stubs.o
obj-y += mt_sleep.o
obj-y += mt_spm.o
# obj-y += mt_spm_dpidle.o  # m681: defer runtime-PM (idle/dpidle/sodi/vcorefs) for boot
# obj-y += mt_spm_dpidle_mt6755.o  # m681: defer runtime-PM (idle/dpidle/sodi/vcorefs) for boot
obj-y += mt_spm_fs.o
obj-y += mt_spm_internal.o
obj-y += mt_spm_pmic_wrap.o
obj-y += mt_spm_sleep.o
# obj-y += mt_spm_sodi.o  # m681: defer runtime-PM (idle/dpidle/sodi/vcorefs) for boot
# obj-y += mt_spm_sodi3p0.o  # m681: defer runtime-PM (idle/dpidle/sodi/vcorefs) for boot
# obj-y += mt_spm_sodi_cmdq.o  # m681: defer runtime-PM (idle/dpidle/sodi/vcorefs) for boot
# obj-y += mt_spm_vcorefs_mt6755.o  # m681: defer runtime-PM (idle/dpidle/sodi/vcorefs) for boot
endif

ifeq ($(CONFIG_MACH_MT6757), y)
#ccflags-y += -I$(srctree)/sound/soc/mediatek/mt_soc_audio_6757/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/power_gs_v1/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/dramc/mt6757/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/cameraisp/src/$(MTK_PLATFORM)/inc/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/m4u/$(MTK_PLATFORM)/

ifeq ($(CONFIG_MTK_PMIC_CHIP_MT6355),y)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include/mt6355/
endif

obj-y += mtk_sleep.o
obj-y += mtk_spm_init.o
obj-y += mtk_spm.o
obj-y += mtk_spm_internal.o
obj-y += mtk_spm_pmic_wrap.o
obj-y += mtk_spm_sleep.o
obj-y += mtk_spm_fs.o
obj-y += mtk_idle.o
obj-y += mtk_idle_profile.o
obj-y += mtk_idle_mt6757.o
obj-y += mtk_spm_dpidle.o
obj-y += mtk_spm_dpidle_mt6757.o
obj-y += mtk_spm_sodi.o
obj-y += mtk_spm_sodi3p0.o
obj-y += mtk_spm_sodi_cmdq.o
obj-y += mtk_spm_mcsodi.o
obj-y += mtk_spm_vcorefs_mt6757.o
obj-y += mtk_spm_resource_req.o
endif

#ifeq ($(CONFIG_ARCH_MT6797), y)
#obj-y += mt_idle.o
#obj-y += mt_idle_profile.o
#obj-y += mt_idle_mt6797.o
#obj-y += mt_spm.o
#obj-y += mt_spm_sleep.o
#obj-y += mt_sleep.o
#obj-y += mt_spm_internal.o
#obj-y += mt_spm_dpidle.o
#obj-y += mt_spm_dpidle_mt6797.o
#obj-y += mt_spm_fs.o
#obj-y += mt_spm_sodi.o
#obj-y += mt_spm_sodi3p0.o
#obj-y += mt_spm_sodi_cmdq.o
#obj-y += mt_spm_pmic_wrap.o
#obj-y += mt_spm_vcorefs_mt6797.o
#endif

ifeq ($(CONFIG_MACH_KIBOPLUS), y)
#ccflags-y += -I$(srctree)/sound/soc/mediatek/mt_soc_audio_6757/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/power_gs_v1/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/dramc/mt6757/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/cameraisp/src/$(MTK_PLATFORM)/inc/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/m4u/$(MTK_PLATFORM)/

obj-y += mtk_sleep.o
obj-y += mtk_spm_init.o
obj-y += mtk_spm.o
obj-y += mtk_spm_internal.o
obj-y += mtk_spm_pmic_wrap.o
obj-y += mtk_spm_sleep.o
obj-y += mtk_spm_fs.o
obj-y += mtk_idle.o
obj-y += mtk_idle_profile.o
obj-y += mtk_idle_mt6757.o
obj-y += mtk_spm_dpidle.o
obj-y += mtk_spm_dpidle_mt6757.o
obj-y += mtk_spm_sodi.o
obj-y += mtk_spm_sodi3p0.o
obj-y += mtk_spm_sodi_cmdq.o
obj-y += mtk_spm_mcsodi.o
obj-y += mtk_spm_vcorefs_mt6757.o
obj-y += mtk_spm_resource_req.o
endif
