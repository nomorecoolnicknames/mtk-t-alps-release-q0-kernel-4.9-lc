/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _COMMON_MTK_VCOREFS_MANAGER_H
#define _COMMON_MTK_VCOREFS_MANAGER_H

/* m681: route MACH_MT6755 to the mt6757 vcorefs manager DECLARATIONS
 * (is_vcorefs_can_work / vcorefs_request_dvfs_opp + the governor enums).
 * vcorefs impl deferred; msdc autok_dvfs only needs the decls. Link stubs
 * live in base/power/mt6755/mt_vcorefs_stub.c. Same bridge as the 4.4 tree. */
#if defined(CONFIG_MACH_MT6757) || defined(CONFIG_MACH_KIBOPLUS) || defined(CONFIG_MACH_MT6755)

#include "vcorefs_v1/mtk_vcorefs_manager_mt6757.h"

#elif defined(CONFIG_MACH_MT6799) || defined(CONFIG_MACH_MT6763) || defined(CONFIG_MACH_MT6771)  \
	|| defined(CONFIG_MACH_MT6759) || defined(CONFIG_MACH_MT6758) || defined(CONFIG_MACH_MT6739) \
	|| defined(CONFIG_MACH_MT6775)

#include "vcorefs_v3/mtk_vcorefs_manager.h"

#endif

#endif /* _COMMON_MTK_VCOREFS_MANAGER_H */

