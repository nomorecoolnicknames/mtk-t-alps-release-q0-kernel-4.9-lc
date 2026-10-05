#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/init.h>
#ifdef CONFIG_M3NOTE_FACTORY_PAIR_DIAGNOSTIC
#include <linux/m3note_factory.h>
#endif
#include <linux/m3note_board.h>

static enum m3note_board_id m3note_physical_board __ro_after_init =
	M3NOTE_BOARD_UNKNOWN;

#ifdef CONFIG_M3NOTE_FACTORY_PAIR_DIAGNOSTIC
static const struct m3note_factory_pair m3note_factory_pairs[] = {
#include "m3note_factory_pairs.inc"
};

static int __init m3note_factory_identity_init(void)
{
	bool m681 = of_machine_is_compatible("meizu,m681");
	bool l681 = of_machine_is_compatible("meizu,l681");
	enum m3note_board_id selected = M3NOTE_BOARD_UNKNOWN;

	if (m681 != l681)
		selected = m681 ? M3NOTE_BOARD_M681 : M3NOTE_BOARD_L681;
	m3note_physical_board = m3note_factory_parse(saved_command_line,
		m3note_factory_pairs, ARRAY_SIZE(m3note_factory_pairs), selected);
	return 0;
}
early_initcall(m3note_factory_identity_init);
#endif

/* Written once during early init; there is no writable runtime selector. */
enum m3note_board_id __weak m3note_board_id(void)
{
	return m3note_physical_board;
}

static const char *m3note_selected_dt(bool m681, bool l681)
{
	if (m681 == l681)
		return "unknown";
	return m681 ? "m681" : "l681";
}

/* Selected DT is metadata, never independent physical-board evidence. */
static int m3note_identity_get(char *buffer, const struct kernel_param *kp)
{
	const char *selected = m3note_selected_dt(
		of_machine_is_compatible("meizu,m681"),
		of_machine_is_compatible("meizu,l681"));

	(void)kp;
	return scnprintf(buffer, PAGE_SIZE,
		"schema=1 selected_dt=%s physical=%s verified=%u origin=%s\n",
		selected, m3note_physical_board == M3NOTE_BOARD_M681 ? "m681" :
		m3note_physical_board == M3NOTE_BOARD_L681 ? "l681" : "unknown",
		m3note_physical_board != M3NOTE_BOARD_UNKNOWN,
		m3note_physical_board != M3NOTE_BOARD_UNKNOWN ? "bootloader" :
		"unavailable");
}

static const struct kernel_param_ops m3note_identity_ops = {
	.get = m3note_identity_get,
};
module_param_cb(identity, &m3note_identity_ops, NULL, 0444);
