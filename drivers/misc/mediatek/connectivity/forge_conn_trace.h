/* SPDX-License-Identifier: GPL-2.0 */
/*
 * forge_conn_trace.h - m681 consys module-init / power-on breadcrumbs.
 *
 * Ported from the 4.4 m681 tree (8191b444), where connsys power-on is
 * device-proven. On 4.4 the tracer also wrote diag offsets 0x70/0x74/0x94/
 * 0x98 of the marker page; on 4.9 those offsets belong to the initcall and
 * of-node trackers (init/forge_m681_marker.c), so this version keeps only:
 *   - the rolling marker slot (forge_m681_mark_aux: stage byte + step), and
 *   - a pr_err mirror. Since ef1a5843c the 0x5f000000 ram console survives a
 *     WDT reset into TWRP, so the dmesg mirror is the primary record.
 * Stage bytes (unused elsewhere on 4.9; the bootguard owns 0xDB-0xDF):
 *   0xD7 = wmt_detect / DO_MODULE_INIT path
 *   0xD8 = consys power-on ladder (func-on / reg_ctrl)
 * Process context only (ioctl / worker / sysfs writer), never from an ISR.
 */
#ifndef _FORGE_CONN_TRACE_H
#define _FORGE_CONN_TRACE_H

#include <linux/printk.h>

#define FORGE_CONN_TRACE_DETECT	0xD7	/* module-init / detect path */
#define FORGE_CONN_TRACE_PWRON	0xD8	/* power-on ladder path */

/* init/forge_m681_marker.c; no-op before the marker page is mapped */
extern void forge_m681_mark_aux(u8 stage, u32 aux);

static inline void forge_conn_trace(unsigned char stage, unsigned int step)
{
	forge_m681_mark_aux(stage, step);
	pr_err("[FORGE_CONN_TRACE] %02X:%06X\n", stage, step & 0xFFFFFFu);
}

#endif /* _FORGE_CONN_TRACE_H */
