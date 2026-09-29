/*
 * m681 (2026-07-16): mt6755 compat shim for the mt6757-era header name.
 *
 * The shared 4.4 eccci core includes <mach/mtk_pbm.h> unconditionally
 * (modem_cldma.c:47, modem_ccif_c2k.c:48) - that is the NEWER MTK naming, and the
 * header only exists under mt-plat/mt6757/. The mt6755 base ships the same content
 * under the OLDER name mt_pbm.h (same enum pbm_kicker, same
 * init_md_section_level()/kicker_pbm_by_md() prototypes).
 *
 * Forwarding here keeps the shared eccci core unforked: editing the core would
 * diverge it from the mt6757 platform that also builds against it.
 *
 * NOTE: the PBM *implementation* is still deferred on this route - pbm_v1/ is gated
 * on CONFIG_MACH_MT6757 (base/power/Makefile:23) and mt6755's own mt_pbm.o is
 * commented out (mt6755/Makefile:47). So init_md_section_level() resolves to the
 * no-op in mt_misc_stubs.c; the modem simply does not participate in power
 * budgeting. Remove that stub when PBM is un-deferred.
 */
#ifndef __M681_MACH_MTK_PBM_COMPAT_H__
#define __M681_MACH_MTK_PBM_COMPAT_H__

#include <mach/mt_pbm.h>

#endif /* __M681_MACH_MTK_PBM_COMPAT_H__ */
