/*
 * m681 4.4: mtk_-named shim for the mt6757-vintage thermal code
 * (mtk_ts_cpu.c, mtk_cooler_atm.c include <mach/mtk_clkmgr.h>).
 * The mt6757 donor version of this header is empty (guard only) and
 * the thermal consumers use no clkmgr symbols, so mirror that.
 * Deliberately NOT including mach/mt_clkmgr.h: distinct guard here
 * (_MT_CLKMGR_H is taken by mt_clkmgr.h).
 */
#ifndef __MTK_CLKMGR_SHIM_H__
#define __MTK_CLKMGR_SHIM_H__

#endif
