#ifndef _MTK_GPUFREQ_H
#define _MTK_GPUFREQ_H

/*
 * m681 4.4: this header used to carry static-inline stubs of the
 * mt_gpufreq_* API.  Those redefine the real global definitions in
 * mt_gpufreq_bringup.c (built obj-y) and collide with the weak
 * fallbacks the thermal code carries (e.g. mtk_thermal_platform.c,
 * mtk_cooler_atm.c) once CONFIG_THERMAL=y compiles them.
 * Follow the mt6757 pattern instead: extern declarations only
 * (mt_gpufreq.h); the single real definition set lives in
 * mt_gpufreq_bringup.c.
 */
#include "mt_gpufreq.h"

#endif
