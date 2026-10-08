/*
* Copyright (C) 2016 MediaTek Inc.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/usb/gadget.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
/*#include "mach/emi_mpu.h"*/

#ifdef CONFIG_TCPC_CLASS
#include "tcpm.h"
#endif /* CONFIG_TCPC_CLASS */
#include "mu3d_hal_osal.h"
#include "musb_core.h"
#if defined(CONFIG_MTK_UART_USB_SWITCH) || defined(CONFIG_MTK_SIB_USB_SWITCH)
#include "mtk-phy-asic.h"
/*#include <mach/mt_typedefs.h>*/
#endif

/* #define USB_FORCE_ON */
/* USB FORCE ON for FPGA/U3_COMPLIANCE cases */
/* m681 4.9: also on CONFIG_USB_MU3D_FORCE_ON -- the PMIC (MT6351) layer of this
 * tree is a stub, so mt_get_charger_type()/upmu_is_chr_det() always read "no
 * charger" and connection_work()/musb_suspend_work() would keep the controller
 * stopped/powered down. The 4.4 lane had a live PMIC driver here instead. */
#if defined(CONFIG_FPGA_EARLY_PORTING) || defined(U3_COMPLIANCE) || defined(FOR_BRING_UP) \
        || defined(CONFIG_USB_MU3D_FORCE_ON)
#define USB_FORCE_ON
#endif

unsigned int cable_mode = CABLE_MODE_NORMAL;


































static int forge_usb_keepalive = 1;
module_param(forge_usb_keepalive, int, 0644);
MODULE_PARM_DESC(forge_usb_keepalive,
        "m681: 1 = connection_work must not musb_stop() an enumerated gadget");






static int forge_usb_rescue;
module_param(forge_usb_rescue, int, 0644);
MODULE_PARM_DESC(forge_usb_rescue,
        "m681: 1 = keep the USB watchdog armed after boot to un-wedge is_active=0");
static unsigned int forge_usb_keepalive_saves;
static unsigned int forge_usb_rescue_count;
static unsigned int forge_usb_rescue_susp_defers;
static unsigned int forge_usb_rescue_vbus_defers;
#ifdef CONFIG_MTK_UART_USB_SWITCH
u32 port_mode = PORT_MODE_USB;
u32 sw_tx;
u32 sw_rx;
u32 sw_uart_path;
#endif

/* ================================ */
/* connect and disconnect functions */
/* ================================ */
bool mt_usb_is_device(void)
{
#if !defined(CONFIG_FPGA_EARLY_PORTING) && defined(CONFIG_USB_XHCI_MTK)
        bool tmp = mtk_is_host_mode();

        os_printk(K_INFO, "%s mode\n", tmp ? "HOST" : "DEV");
        return !tmp;
#else
        return true;
#endif
}

enum status { INIT, ON, OFF };
#ifdef CONFIG_USBIF_COMPLIANCE
static enum status connection_work_dev_status = INIT;
void init_connection_work(void)
{
        connection_work_dev_status = INIT;
}
#else
/* FORGE m681 #S3: was a function-local static inside connection_work().  It is
 * the variable that decides whether a "cable IN" event is allowed to call
 * musb_start() at all (see the ON/OFF latch there), so a wedge diagnosis is
 * impossible without reading it.  Lifted to file scope, same storage class and
 * same initial value — no behaviour change — so forge_usb_state_show() can
 * report it.  The CONFIG_USBIF_COMPLIANCE arm above already had it at file
 * scope; this only makes the non-compliance build match. */
static enum status connection_work_dev_status = INIT;
#endif

#ifndef CONFIG_USBIF_COMPLIANCE

struct timespec connect_timestamp = { 0, 0 };

void set_connect_timestamp(void)
{
        connect_timestamp = CURRENT_TIME;
        pr_debug("set timestamp = %llu\n", timespec_to_ns(&connect_timestamp));
}

void clr_connect_timestamp(void)
{
        connect_timestamp.tv_sec = 0;
        connect_timestamp.tv_nsec = 0;
        pr_debug("clr timestamp = %llu\n", timespec_to_ns(&connect_timestamp));
}

struct timespec get_connect_timestamp(void)
{
        pr_debug("get timestamp = %llu\n", timespec_to_ns(&connect_timestamp));
        return connect_timestamp;
}
#endif

void connection_work(struct work_struct *data)
{
        struct musb *musb = container_of(to_delayed_work(data), struct musb, connection_work);


#ifdef CONFIG_MTK_UART_USB_SWITCH
        if (!usb_phy_check_in_uart_mode()) {
#endif
                bool is_usb_cable = usb_cable_connected();

#ifndef CONFIG_FPGA_EARLY_PORTING

                if (!mt_usb_is_device()) {
                        connection_work_dev_status = OFF;
                        usb_fake_powerdown(musb->is_clk_on);
                        musb->is_clk_on = 0;
                        os_printk(K_INFO, "%s, Host mode. directly return\n", __func__);
                        return;
                }
#endif

                os_printk(K_INFO, "%s musb %s, cable %s\n", __func__,
                          ((connection_work_dev_status ==
                            0) ? "INIT" : ((connection_work_dev_status == 1) ? "ON" : "OFF")),
                          (is_usb_cable ? "IN" : "OUT"));

                if ((is_usb_cable == true) && (connection_work_dev_status != ON)) {

                        connection_work_dev_status = ON;
#ifndef CONFIG_USBIF_COMPLIANCE
                        set_connect_timestamp();
#endif

                        if (!musb->usb_wakelock.active)
                                __pm_stay_awake(&musb->usb_wakelock);

                        /* FIXME: Should use usb_udc_start() & usb_gadget_connect(), like usb_udc_softconn_store().
                         * But have no time to think how to handle. However i think it is the correct way.
                         */
                        musb_start(musb);

                        os_printk(K_INFO, "%s ----Connect----\n", __func__);
                } else if ((is_usb_cable == false) && (connection_work_dev_status != OFF)) {

                        /* FORGE m681 #S1: do not tear down a bus that the host
                         * still owns.  musb->g.speed is set by musb_g_reset()
                         * (host bus reset) and cleared by musb_g_disconnect()
                         * from the DISCONN_INTR handler, so "speed != UNKNOWN"
                         * means the U2 MAC currently sees an enumerated host —
                         * which outranks a charger-stack cable-out reading.
                         * On a genuine unplug the DISCONN interrupt clears
                         * g.speed first, so this branch still runs and the
                         * wakelock/clock are still released. */
                        if (forge_usb_keepalive && musb->g.speed != USB_SPEED_UNKNOWN
                            && musb->softconnect && musb->is_active) {
                                forge_usb_keepalive_saves++;
                                pr_emerg("[FORGE_M681] usb keepalive: REFUSED musb_stop, host still enumerated (speed=%d softconn=%d active=%d clk=%d chg_type=%d) saves=%u\n",
                                         (int)musb->g.speed, (int)musb->softconnect,
                                         (int)musb->is_active, (int)musb->is_clk_on,
                                         (int)musb->charger_mode,
                                         forge_usb_keepalive_saves);
                                return;
                        }
                        /* Un-blind the teardown: os_printk(K_INFO) below is
                         * gated off by the default debug_level, so every dmesg
                         * this project ever captured was silent here. */
                        pr_emerg("[FORGE_M681] usb teardown: musb_stop (speed=%d softconn=%d active=%d clk=%d chg_type=%d keepalive=%d)\n",
                                 (int)musb->g.speed, (int)musb->softconnect,
                                 (int)musb->is_active, (int)musb->is_clk_on,
                                 (int)musb->charger_mode, forge_usb_keepalive);

                        connection_work_dev_status = OFF;
#ifndef CONFIG_USBIF_COMPLIANCE
                        clr_connect_timestamp();
#endif

                        /*FIXME: we should use usb_gadget_disconnect() & usb_udc_stop().  like usb_udc_softconn_store().
                         * But have no time to think how to handle. However i think it is the correct way.
                         */
                        musb_stop(musb);

                        if (musb->usb_wakelock.active)
                                __pm_relax(&musb->usb_wakelock);

                        os_printk(K_INFO, "%s ----Disconnect----\n", __func__);
                } else {
                        /* This if-elseif is to set wakelock when booting with USB cable.
                         * Because battery driver does _NOT_ notify at this codition.
                         */
                        /* if( (is_usb_cable == true) && !wake_lock_active(&musb->usb_wakelock)) { */
                        /* os_printk(K_INFO, "%s Boot wakelock\n", __func__); */
                        /* wake_lock(&musb->usb_wakelock); */
                        /* } else if( (is_usb_cable == false) && wake_lock_active(&musb->usb_wakelock)) { */
                        /* os_printk(K_INFO, "%s Boot unwakelock\n", __func__); */
                        /* wake_unlock(&musb->usb_wakelock); */
                        /* } */

                        os_printk(K_INFO, "%s directly return\n", __func__);
                }
#ifdef CONFIG_MTK_UART_USB_SWITCH
        } else {
#if 0
                usb_fake_powerdown(musb->is_clk_on);
                musb->is_clk_on = 0;
#else
                os_printk(K_INFO, "%s, in UART MODE!!!\n", __func__);
#endif
        }
#endif
}

bool mt_usb_is_ready(void)
{
        os_printk(K_INFO, "USB is ready or not\n");
#ifdef NEVER
        if (!mtk_musb || !mtk_musb->is_ready)
                return false;
        else
                return true;
#endif				/* NEVER */
        return true;
}

void mt_usb_connect(void)
{
        os_printk(K_INFO, "%s+\n", __func__);
        if (_mu3d_musb) {
                struct delayed_work *work;

                work = &_mu3d_musb->connection_work;

                schedule_delayed_work_on(0, work, 0);
        } else {
                os_printk(K_INFO, "%s musb_musb not ready\n", __func__);
        }
        os_printk(K_INFO, "%s-\n", __func__);
}
EXPORT_SYMBOL_GPL(mt_usb_connect);

/* m681: forge USB force-connect kicker (ported from the 3.18 m6-graft v148/v149).
 * On this board the VBUS/charger-detect path that normally calls mt_usb_connect()
 * does NOT assert "cable in" (pmic reports "No charger"), so the gadget never gets
 * connected and adb never enumerates even though adbd is up and has bound the
 * functionfs composite to the UDC. Force it ourselves: once musb->gadget_driver is
 * bound, set usb_rdy (open connection_work's gate) and kick connection_work ->
 * musb_start -> D+ pullup -> host enumerates adb. Kick every 2s for ~2min to cover
 * late adbd bind and to re-assert after the android config-switch disconnect. */
extern void set_usb_rdy(void);
extern void mu3d_hal_u2dev_connect(void);	/* the actual U2 D+ pullup (musb_pullup's body) */
static struct delayed_work forge_usb_kick_work;
static int forge_usb_kick_n;
#define FORGE_USB_KICK_BOOT_N	60	/* boot phase: 60 kicks x 2 s (unchanged) */
#define FORGE_USB_WATCH_MS	10000	/* steady state: piggy-backs on the 10 s
                                         * BAT_TASK_PERIOD wake, so no new
                                         * wakeup class is introduced */
/* FORGE m681 #S1 instrument, deliberately on a DIFFERENT mechanism from the
 * keepalive guard: sample the two inputs of usb_cable_connected() directly
 * (pure reads, no side effect on musb->charger_mode) and print only on change.
 * The only pre-existing readout of this predicate was
 * battery_common_fg_20.c's "pass#%d chr_exist=" line, which is hard-bounded to
 * the first 5 battery passes (forge_bat_logn < 5) — i.e. seconds ~92-97 of a
 * boot — so no capture this project holds can say whether chrdet later
 * dropped.  This closes that blind spot without touching the charger lane's
 * file. */
static int forge_usb_chrwatch = 1;
module_param(forge_usb_chrwatch, int, 0644);
MODULE_PARM_DESC(forge_usb_chrwatch,
        "m681: 1 = log every change of the (chg_type, vbus) pair that decides USB life");

static void forge_usb_chrwatch_sample(struct musb *musb)
{
        static int last = -1;
        int type, vbus, now;

        if (!forge_usb_chrwatch)
                return;

        type = (int)mt_get_charger_type();
        vbus = upmu_is_chr_det() ? 1 : 0;
        now = (type << 1) | vbus;
        if (now == last)
                return;
        last = now;
        pr_emerg("[FORGE_M681] usb chrwatch: chg_type=%d vbus=%d -> cable_connected would be %d (gadget speed=%d softconn=%d active=%d)\n",
                 type, vbus,
                 (vbus && (type == STANDARD_HOST || type == CHARGING_HOST)) ? 1 : 0,
                 musb ? (int)musb->g.speed : -1,
                 musb ? (int)musb->softconnect : -1,
                 musb ? (int)musb->is_active : -1);
}

static void forge_usb_kick_fn(struct work_struct *w)
{
        struct musb *musb = _mu3d_musb;

        forge_usb_chrwatch_sample(musb);

























































        if (forge_usb_kick_n >= FORGE_USB_KICK_BOOT_N) {
                static int forge_usb_broken_seen;
                static int forge_usb_last_defer;
                bool bound = musb && musb->gadget_driver && musb->softconnect;
                bool wedged = bound && !musb->is_active;
                bool parked = bound && musb->is_suspended;
                bool vbus = upmu_is_chr_det() ? true : false;
                int defer = 0;

                if (wedged && parked)
                        defer = 1;
                else if (wedged && !vbus)
                        defer = 2;

                if (forge_usb_rescue && wedged && !defer) {
                        if (++forge_usb_broken_seen >= 2) {
                                forge_usb_broken_seen = 0;
                                forge_usb_rescue_count++;
                                pr_emerg("[FORGE_M681] usb RESCUE #%u: gadget bound + softconnect, is_active=0, NOT suspended, vbus=1 -> musb_start + D+ pullup (clk=%d speed=%d)\n",
                                         forge_usb_rescue_count,
                                         (int)musb->is_clk_on, (int)musb->g.speed);
                                musb_start(musb);
                                if (musb->is_clk_on && musb->softconnect)
                                        mu3d_hal_u2dev_connect();
                                set_usb_rdy();
                        }
                } else {
                        forge_usb_broken_seen = 0;
                        /* Print only on change: this arm is the steady state
                         * whenever the phone is unplugged or parked, so an
                         * unconditional print would flood the ring every 10 s
                         * (the ring is already heartbeat-saturated). */
                        if (defer && defer != forge_usb_last_defer) {
                                if (defer == 1)
                                        forge_usb_rescue_susp_defers++;
                                else
                                        forge_usb_rescue_vbus_defers++;
                                pr_emerg("[FORGE_M681] usb rescue DEFERRED (%s): speed=%d softconn=%d active=%d susp=%d clk=%d vbus=%d defers=susp:%u/vbus:%u\n",
                                         defer == 1 ? "host parked the port"
                                                    : "no VBUS, cable is out",
                                         (int)musb->g.speed, (int)musb->softconnect,
                                         (int)musb->is_active, (int)musb->is_suspended,
                                         (int)musb->is_clk_on, (int)vbus,
                                         forge_usb_rescue_susp_defers,
                                         forge_usb_rescue_vbus_defers);
                        }
                }
                forge_usb_last_defer = defer;
                schedule_delayed_work(&forge_usb_kick_work,
                                      msecs_to_jiffies(FORGE_USB_WATCH_MS));
                return;
        }

        if (musb && musb->gadget_driver) {
                /* ROOT CAUSE (observed via the usbkick markers): the gadget binds and sets
                 * softconnect=1, but musb->is_active stays 0, so musb_pullup() bails with
                 * "power and clk is not ready" and the D+ pullup never happens -> host never
                 * enumerates. is_active is set by musb_start(), which connection_work() only
                 * calls once (its dev_status latches ON); after the android config-switch
                 * disconnect resets is_active=0, connection_work no longer re-musb_start()s.
                 * Fix: drive it directly here. musb_start() turns the clock on and sets
                 * is_active=1 + enables the controller; then assert the U2 D+ pullup that
                 * musb_pullup() would have done. */
                if (!musb->is_active || !musb->is_clk_on)	/* clk=0: #S4 trap */
                        musb_start(musb);
                if (musb->is_clk_on && musb->softconnect)
                        mu3d_hal_u2dev_connect();
                set_usb_rdy();
                pr_emerg("[FORGE_M681] usbkick n=%d gdrv=%p softconn=%d active=%d clk=%d\n",
                         forge_usb_kick_n, musb->gadget_driver,
                         musb->softconnect, musb->is_active, musb->is_clk_on);
        } else {
                pr_emerg("[FORGE_M681] usbkick n=%d waiting gadget_driver (musb=%p)\n",
                         forge_usb_kick_n, musb);
        }
        forge_usb_kick_n++;
        schedule_delayed_work(&forge_usb_kick_work,
                              msecs_to_jiffies(forge_usb_kick_n < FORGE_USB_KICK_BOOT_N
                                               ? 2000 : FORGE_USB_WATCH_MS));
}




























/* m681 bootguard hook (init/forge_m681_marker.c): the same pure reads as
 * forge_usb_state_show() below, pushed to the log when the host loses the
 * USB configuration and again right before the bootguard restarts, so the
 * capture shows which state the controller was left in. */
void forge_usb_log_state(const char *why)
{
        struct musb *musb = _mu3d_musb;

        if (!musb) {
                pr_emerg("[FORGE_M681] usb state (%s): musb=NULL\n", why);
                return;
        }
        pr_emerg("[FORGE_M681] usb state (%s): gadget_driver=%d softconn=%d active=%d susp=%d clk=%d speed=%d dev_status=%s chrdet=%d kick_n=%d\n",
                 why, musb->gadget_driver ? 1 : 0, (int)musb->softconnect,
                 (int)musb->is_active, (int)musb->is_suspended,
                 (int)musb->is_clk_on, (int)musb->g.speed,
                 connection_work_dev_status == INIT ? "INIT" :
                 (connection_work_dev_status == ON ? "ON" : "OFF"),
                 upmu_is_chr_det() ? 1 : 0, forge_usb_kick_n);
}












extern s32 pwrap_read(u32 adr, u32 *rdata);
#define FORGE_MT6351_CHR_CON0	0x0F78	/* b5 RGS_CHRDET */
#define FORGE_USB_VBUS_MS	1000
#define FORGE_USB_VBUS_LOW_N	2
static int forge_usb_vbus_reinit_en = 1;
module_param(forge_usb_vbus_reinit_en, int, 0644);
MODULE_PARM_DESC(forge_usb_vbus_reinit_en,
        "m681: 1 = restart the controller when VBUS (CHRDET) returns after a cable-out");
static struct delayed_work forge_usb_vbus_work;
static int forge_usb_vbus_last = -1;
static int forge_usb_vbus_low_n;
static unsigned int forge_usb_vbus_falls, forge_usb_vbus_rises;
static unsigned int forge_usb_vbus_reinits, forge_usb_vbus_skips;

static int forge_usb_vbus_read(void)
{
        u32 con0;

        if (pwrap_read(FORGE_MT6351_CHR_CON0, &con0))
                return -1;
        return (con0 >> 5) & 1;
}

static void forge_usb_vbus_fn(struct work_struct *w)
{
        struct musb *musb = _mu3d_musb;
        int vbus = forge_usb_vbus_read();

        if (vbus < 0 || !musb)
                goto out;
        if (vbus == 0) {
                if (forge_usb_vbus_last == 1) {
                        forge_usb_vbus_falls++;
                        pr_emerg("[FORGE_M681] usb vbus: cable out #%u (gadget_driver=%d softconn=%d active=%d susp=%d clk=%d speed=%d)\n",
                                 forge_usb_vbus_falls, musb->gadget_driver ? 1 : 0,
                                 (int)musb->softconnect, (int)musb->is_active,
                                 (int)musb->is_suspended, (int)musb->is_clk_on,
                                 (int)musb->g.speed);
                }
                forge_usb_vbus_low_n++;
        } else if (forge_usb_vbus_last == 0) {
                bool act = forge_usb_vbus_reinit_en && musb->gadget_driver &&
                           forge_usb_vbus_low_n >= FORGE_USB_VBUS_LOW_N;

                forge_usb_vbus_rises++;
                pr_emerg("[FORGE_M681] usb vbus: cable in #%u after %d low samples (gadget_driver=%d softconn=%d active=%d susp=%d clk=%d speed=%d) -> %s\n",
                         forge_usb_vbus_rises, forge_usb_vbus_low_n,
                         musb->gadget_driver ? 1 : 0, (int)musb->softconnect,
                         (int)musb->is_active, (int)musb->is_suspended,
                         (int)musb->is_clk_on, (int)musb->g.speed,
                         act ? "controller restart" : "left alone");
                if (act) {
                        forge_usb_vbus_reinits++;
                        forge_usb_vbus_reinit(musb);
                        pr_emerg("[FORGE_M681] usb vbus: restart #%u done (softconn=%d active=%d clk=%d)\n",
                                 forge_usb_vbus_reinits, (int)musb->softconnect,
                                 (int)musb->is_active, (int)musb->is_clk_on);
                } else {
                        forge_usb_vbus_skips++;
                }
        }
        if (vbus == 1)
                forge_usb_vbus_low_n = 0;
        forge_usb_vbus_last = vbus;
out:
        queue_delayed_work(system_freezable_power_efficient_wq,
                           &forge_usb_vbus_work,
                           msecs_to_jiffies(FORGE_USB_VBUS_MS));
}

static int forge_usb_state_show(struct seq_file *m, void *v)
{
        struct musb *musb = _mu3d_musb;

        if (!musb) {
                seq_puts(m, "musb=NULL\n");
                return 0;
        }

        seq_printf(m, "gadget_driver=%d softconn=%d active=%d susp=%d clk=%d speed=%d\n",
                   musb->gadget_driver ? 1 : 0, (int)musb->softconnect,
                   (int)musb->is_active, (int)musb->is_suspended,
                   (int)musb->is_clk_on, (int)musb->g.speed);
        /* Reproduce usb_cable_connected()'s verdict from PURE READS.  Calling
         * usb_cable_connected() itself here would be wrong: it assigns
         * _mu3d_musb->charger_mode = mt_get_charger_type() as a side effect, and
         * musb_start() branches on musb->charger_mode (u3dev_en vs
         * u2dev_connect) — an instrument must not write the state it reports.
         * Same pure-read construction as forge_usb_chrwatch_sample(). */
        {
                int type = (int)mt_get_charger_type();
                int vbus = upmu_is_chr_det() ? 1 : 0;
                int would = (vbus && (type == STANDARD_HOST || type == CHARGING_HOST)
                             && cable_mode == CABLE_MODE_NORMAL) ? 1 : 0;

                seq_printf(m, "dev_status=%s cable_mode=%u charger_mode=%d chg_type=%d chrdet=%d cable_connected_would_be=%d\n",
                           connection_work_dev_status == INIT ? "INIT" :
                           (connection_work_dev_status == ON ? "ON" : "OFF"),
                           cable_mode, (int)musb->charger_mode, type, vbus, would);
        }
        seq_printf(m, "knobs: rescue=%d keepalive=%d chrwatch=%d suspend_guard=%d kick_n=%d\n",
                   forge_usb_rescue, forge_usb_keepalive, forge_usb_chrwatch,
                   forge_usb_suspend_guard, forge_usb_kick_n);
        seq_printf(m, "counters: rescues=%u keepalive_saves=%u defer_susp=%u defer_vbus=%u suspend_guard_saves=%u\n",
                   forge_usb_rescue_count, forge_usb_keepalive_saves,
                   forge_usb_rescue_susp_defers, forge_usb_rescue_vbus_defers,
                   forge_usb_suspend_saves);
        seq_printf(m, "vbus: chrdet=%d last=%d low_n=%d falls=%u rises=%u restarts=%u skips=%u reinit_en=%d\n",
                   forge_usb_vbus_read(), forge_usb_vbus_last,
                   forge_usb_vbus_low_n, forge_usb_vbus_falls,
                   forge_usb_vbus_rises, forge_usb_vbus_reinits,
                   forge_usb_vbus_skips, forge_usb_vbus_reinit_en);
        /* WEDGE verdict: softconn+active but no clock cannot happen while
         * healthy, and nothing in the driver can leave it.  #S4's suspend guard
         * is what should keep this line from ever appearing; if it appears with
         * suspend_guard=1 and suspend_guard_saves=0, the wedge arrived by a route
         * #S4 does not cover and the W1 diagnosis is incomplete. */
        if (musb->softconnect && musb->is_active && !musb->is_clk_on)
                seq_puts(m, "VERDICT: WEDGED (softconn+active but clk=0; SSUSB IP is in SW reset, reboot-only)\n");
        return 0;
}

static int forge_usb_state_open(struct inode *inode, struct file *file)
{
        return single_open(file, forge_usb_state_show, NULL);
}

static const struct file_operations forge_usb_state_fops = {
        .owner = THIS_MODULE,
        .open = forge_usb_state_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = single_release,
};

static int __init forge_usb_kick_init(void)
{
        INIT_DELAYED_WORK(&forge_usb_kick_work, forge_usb_kick_fn);
        schedule_delayed_work(&forge_usb_kick_work, msecs_to_jiffies(7000));
        INIT_DELAYED_WORK(&forge_usb_vbus_work, forge_usb_vbus_fn);
        queue_delayed_work(system_freezable_power_efficient_wq,
                           &forge_usb_vbus_work, msecs_to_jiffies(FORGE_USB_VBUS_MS));
        if (!proc_create("m681_usb_state", 0444, NULL, &forge_usb_state_fops))
                pr_emerg("[FORGE_M681] /proc/m681_usb_state create FAILED\n");
        pr_emerg("[FORGE_M681] USB force-connect kicker armed (v149-port, #S3 rescue gating, cable force_on=%d)\n",
                 IS_ENABLED(CONFIG_USB_MU3D_FORCE_ON));
        return 0;
}
late_initcall(forge_usb_kick_init);

void mt_usb_disconnect(void)
{
        os_printk(K_INFO, "%s+\n", __func__);

        if (_mu3d_musb) {
                struct delayed_work *work;

                work = &_mu3d_musb->connection_work;

                schedule_delayed_work_on(0, work, 0);
        } else {
                os_printk(K_INFO, "%s musb_musb not ready\n", __func__);
        }
        os_printk(K_INFO, "%s-\n", __func__);
}
EXPORT_SYMBOL_GPL(mt_usb_disconnect);

bool usb_cable_connected(void)
{
        CHARGER_TYPE chg_type = CHARGER_UNKNOWN;
        bool connected = false, vbus_exist = false;
#if 0
#ifdef CONFIG_MTK_KERNEL_POWER_OFF_CHARGING
        if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT
                        || get_boot_mode() == LOW_POWER_OFF_CHARGING_BOOT) {
                os_printk(K_INFO, "%s, in KPOC, force USB on\n", __func__);
                return true;
        }
#endif
#endif
#ifdef USB_FORCE_ON
        /* FORCE USB ON */
        chg_type = STANDARD_HOST;
        if (_mu3d_musb)	/* m681 4.9: callable before the controller exists */
                _mu3d_musb->charger_mode = STANDARD_HOST;
        vbus_exist = true;
        connected = true;
        os_printk(K_INFO, "%s type force to STANDARD_HOST\n", __func__);
#else
        /* TYPE CHECK*/
        chg_type = _mu3d_musb->charger_mode = mt_get_charger_type();
        if (fake_CDP && chg_type == STANDARD_HOST) {
                os_printk(K_INFO, "%s, fake to type 2\n", __func__);
                chg_type = CHARGING_HOST;
        }

        if (chg_type == STANDARD_HOST || chg_type == CHARGING_HOST)
                connected = true;

        /* VBUS CHECK to avoid type miss-judge */
#ifdef CONFIG_POWER_EXT
        vbus_exist = upmu_get_rgs_chrdet();
#else
        vbus_exist = upmu_is_chr_det();
#endif
        os_printk(K_INFO, "%s vbus_exist=%d type=%d\n", __func__, vbus_exist, chg_type);
        if (!vbus_exist)
                connected = false;
#endif

        /* CMODE CHECK */
        if (cable_mode == CABLE_MODE_CHRG_ONLY || (cable_mode == CABLE_MODE_HOST_ONLY && chg_type != CHARGING_HOST))
                connected = false;

        os_printk(K_INFO, "%s, connected:%d, cable_mode:%d\n", __func__, connected, cable_mode);
        return connected;
}
EXPORT_SYMBOL_GPL(usb_cable_connected);

#ifdef CONFIG_USB_C_SWITCH
int typec_switch_usb_connect(void *data)
{
        struct musb *musb = data;

        os_printk(K_INFO, "%s+\n", __func__);

        if (musb && musb->gadget_driver) {
                struct delayed_work *work;

                work = &musb->connection_work;

                schedule_delayed_work_on(0, work, 0);
        } else {
                os_printk(K_INFO, "%s musb_musb not ready\n", __func__);
        }
        os_printk(K_INFO, "%s-\n", __func__);

        return 0;
}

int typec_switch_usb_disconnect(void *data)
{
        struct musb *musb = data;

        os_printk(K_INFO, "%s+\n", __func__);

        if (musb && musb->gadget_driver) {
                struct delayed_work *work;

                work = &musb->connection_work;

                schedule_delayed_work_on(0, work, 0);
        } else {
                os_printk(K_INFO, "%s musb_musb not ready\n", __func__);
        }
        os_printk(K_INFO, "%s-\n", __func__);

        return 0;
}
#endif

#ifdef NEVER
void musb_platform_reset(struct musb *musb)
{
        u16 swrst = 0;
        void __iomem *mbase = musb->mregs;

        swrst = musb_readw(mbase, MUSB_SWRST);
        swrst |= (MUSB_SWRST_DISUSBRESET | MUSB_SWRST_SWRST);
        musb_writew(mbase, MUSB_SWRST, swrst);
}
#endif				/* NEVER */


void musb_sync_with_bat(struct musb *musb, int usb_state)
{
        os_printk(K_DEBUG, "musb_sync_with_bat\n");

#ifndef CONFIG_FPGA_EARLY_PORTING
#if defined(CONFIG_MTK_SMART_BATTERY)
        BATTERY_SetUSBState(usb_state);
        wake_up_bat();
#endif
#endif

}
EXPORT_SYMBOL_GPL(musb_sync_with_bat);


#ifdef CONFIG_USB_MTK_DUALMODE
bool musb_check_ipo_state(void)
{
        bool ipo_off;

        down(&_mu3d_musb->musb_lock);
        ipo_off = _mu3d_musb->in_ipo_off;
        os_printk(K_INFO, "IPO State is %s\n", (ipo_off ? "true" : "false"));
        up(&_mu3d_musb->musb_lock);
        return ipo_off;
}
#endif

/*--FOR INSTANT POWER ON USAGE--------------------------------------------------*/
static inline struct musb *dev_to_musb(struct device *dev)
{
        return dev_get_drvdata(dev);
}

const char *const usb_mode_str[CABLE_MODE_MAX] = { "CHRG_ONLY", "NORMAL", "HOST_ONLY" };

ssize_t musb_cmode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        if (!dev) {
                os_printk(K_ERR, "dev is null!!\n");
                return 0;
        }
        return sprintf(buf, "%d\n", cable_mode);
}

ssize_t musb_cmode_store(struct device *dev, struct device_attribute *attr,
                         const char *buf, size_t count)
{
        unsigned int cmode;
        struct musb *musb;
#ifdef CONFIG_TCPC_CLASS
        struct tcpc_device *tcpc;
#endif /* CONFIG_TCPC_CLASS */

        if (!dev) {
                os_printk(K_ERR, "dev is null!!\n");
                return count;
        }

#ifdef CONFIG_TCPC_CLASS
        tcpc = tcpc_dev_get_by_name("type_c_port0");
        if (!tcpc) {
                pr_err("%s get tcpc device type_c_port0 fail\n", __func__);
                return -ENODEV;
        }
#endif /* CONFIG_TCPC_CLASS */
        musb = dev_to_musb(dev);

        if (sscanf(buf, "%ud", &cmode) == 1) {
                os_printk(K_INFO, "%s %s --> %s\n", __func__, usb_mode_str[cable_mode],
                          usb_mode_str[cmode]);

                if (cmode >= CABLE_MODE_MAX)
                        cmode = CABLE_MODE_NORMAL;

                if (cable_mode != cmode) {
                        cable_mode = cmode;
                        if (_mu3d_musb) {
                                if (down_interruptible(&_mu3d_musb->musb_lock))
                                        os_printk(K_INFO, "%s: busy, Couldn't get musb_lock\n", __func__);
                        }
                        if (cmode == CABLE_MODE_CHRG_ONLY) {	/* IPO shutdown, disable USB */
                                if (_mu3d_musb)
                                        _mu3d_musb->in_ipo_off = true;
                        } else {	/* IPO bootup, enable USB */
                                if (_mu3d_musb)
                                        _mu3d_musb->in_ipo_off = false;
                        }

                        if (cmode == CABLE_MODE_CHRG_ONLY) {	/* IPO shutdown, disable USB */
                                if (musb) {
                                        musb->usb_mode = CABLE_MODE_CHRG_ONLY;
                                        mt_usb_disconnect();
                                }
                        } else if (cmode == CABLE_MODE_HOST_ONLY) {
                                if (musb) {
                                        musb->usb_mode = CABLE_MODE_HOST_ONLY;
                                        mt_usb_disconnect();
                                }
                        } else {	/* IPO bootup, enable USB */
                                if (musb) {
                                        musb->usb_mode = CABLE_MODE_NORMAL;
#ifndef CONFIG_USB_C_SWITCH
                                        mt_usb_connect();
#else
                                        typec_switch_usb_connect(musb);
#endif
                                }
                        }
#ifdef CONFIG_USB_MTK_DUALMODE
                        if (cmode == CABLE_MODE_CHRG_ONLY) {
                                #ifdef CONFIG_TCPC_CLASS
                                tcpm_typec_change_role(tcpc, TYPEC_ROLE_SNK);
                                #elif defined(CONFIG_USB_MTK_IDDIG)
                                mtk_disable_host();
                                #endif /* CONFIG_TCPC_CLASS */
                        } else {
                                #ifdef CONFIG_TCPC_CLASS
                                tcpm_typec_change_role(tcpc, TYPEC_ROLE_DRP);
                                #elif defined(CONFIG_USB_MTK_IDDIG)
                                mtk_enable_host();
                                #endif /* CONFIG_TCPC_CLASS */
                        }
#endif
                        if (_mu3d_musb)
                                up(&_mu3d_musb->musb_lock);
                }
        }
        return count;
}

#ifdef CONFIG_MTK_UART_USB_SWITCH
ssize_t musb_portmode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        if (!dev) {
                pr_debug("dev is null!!\n");
                return 0;
        }

        if (usb_phy_check_in_uart_mode())
                port_mode = PORT_MODE_UART;
        else
                port_mode = PORT_MODE_USB;

        if (port_mode == PORT_MODE_USB)
                pr_debug("\nUSB Port mode -> USB\n");
        else if (port_mode == PORT_MODE_UART)
                pr_debug("\nUSB Port mode -> UART\n");

        uart_usb_switch_dump_register();

        return scnprintf(buf, PAGE_SIZE, "%d\n", port_mode);
}

ssize_t musb_portmode_store(struct device *dev, struct device_attribute *attr,
                            const char *buf, size_t count)
{
        unsigned int portmode;

        if (!dev) {
                pr_debug("dev is null!!\n");
                return count;
        } else if (sscanf(buf, "%ud", &portmode) == 1) {
                pr_debug("\nUSB Port mode: current => %d (port_mode), change to => %d (portmode)\n",
                         port_mode, portmode);
                if (portmode >= PORT_MODE_MAX)
                        portmode = PORT_MODE_USB;

                if (port_mode != portmode) {
                        if (portmode == PORT_MODE_USB) {	/* Changing to USB Mode */
                                pr_debug("USB Port mode -> USB\n");
                                usb_phy_switch_to_usb();
                        } else if (portmode == PORT_MODE_UART) {	/* Changing to UART Mode */
                                pr_debug("USB Port mode -> UART\n");
                                usb_phy_switch_to_uart();
                        }
                        uart_usb_switch_dump_register();
                        port_mode = portmode;
                }
        }
        return count;
}

ssize_t musb_tx_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        u8 var;
        u8 var2;

        if (!dev) {
                pr_debug("dev is null!!\n");
                return 0;
        }

        var = U3PhyReadReg8((u3phy_addr_t) (U3D_U2PHYDTM1 + 0x2));
        var2 = (var >> 3) & ~0xFE;
        pr_debug("[MUSB]addr: 0x6E (TX), value: %x - %x\n", var, var2);

        sw_tx = var;

        return scnprintf(buf, PAGE_SIZE, "%x\n", var2);
}

ssize_t musb_tx_store(struct device *dev, struct device_attribute *attr,
                      const char *buf, size_t count)
{
        unsigned int val;
        u8 var;
        u8 var2;

        if (!dev) {
                pr_debug("dev is null!!\n");
                return count;
        } else if (sscanf(buf, "%ud", &val) == 1) {
                pr_debug("\n Write TX : %d\n", val);

#ifdef CONFIG_FPGA_EARLY_PORTING
                var = USB_PHY_Read_Register8(U3D_U2PHYDTM1 + 0x2);
#else
                var = U3PhyReadReg8((u3phy_addr_t) (U3D_U2PHYDTM1 + 0x2));
#endif

                if (val == 0)
                        var2 = var & ~(1 << 3);
                else
                        var2 = var | (1 << 3);

#ifdef CONFIG_FPGA_EARLY_PORTING
                USB_PHY_Write_Register8(var2, U3D_U2PHYDTM1 + 0x2);
                var = USB_PHY_Read_Register8(U3D_U2PHYDTM1 + 0x2);
#else
                /* U3PhyWriteField32(U3D_USBPHYDTM1+0x2,
                 * E60802_RG_USB20_BC11_SW_EN_OFST, E60802_RG_USB20_BC11_SW_EN, 0);
                 */
                /* Jeremy TODO 0320 */
                var = U3PhyReadReg8((u3phy_addr_t) (U3D_U2PHYDTM1 + 0x2));
#endif

                var2 = (var >> 3) & ~0xFE;

                pr_debug
                    ("[MUSB]addr: U3D_U2PHYDTM1 (0x6E) TX [AFTER WRITE], value after: %x - %x\n",
                     var, var2);
                sw_tx = var;
        }
        return count;
}

ssize_t musb_rx_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        u8 var;
        u8 var2;

        if (!dev) {
                pr_debug("dev is null!!\n");
                return 0;
        }
#ifdef CONFIG_FPGA_EARLY_PORTING
        var = USB_PHY_Read_Register8(U3D_U2PHYDMON1 + 0x3);
#else
        var = U3PhyReadReg8((u3phy_addr_t) (U3D_U2PHYDMON1 + 0x3));
#endif
        var2 = (var >> 7) & ~0xFE;
        pr_debug("[MUSB]addr: U3D_U2PHYDMON1 (0x77) (RX), value: %x - %x\n", var, var2);
        sw_rx = var;

        return scnprintf(buf, PAGE_SIZE, "%x\n", var2);
}
ssize_t musb_uart_path_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        u8 var = 0;

        if (!dev) {
                pr_debug("dev is null!!\n");
                return 0;
        }

        var = DRV_Reg32(ap_uart0_base + 0x600);
        pr_debug("[MUSB]addr: (GPIO Misc) 0x600, value: %x\n\n", DRV_Reg32(ap_uart0_base + 0x600));
        sw_uart_path = var;

        return scnprintf(buf, PAGE_SIZE, "%x\n", var);
}
#endif

#ifdef CONFIG_MTK_SIB_USB_SWITCH
ssize_t musb_sib_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        int ret;

        if (!dev) {
                pr_debug("dev is null!!\n");
                return 0;
        }
        ret = usb_phy_sib_enable_switch_status();
        return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
}

ssize_t musb_sib_enable_store(struct device *dev, struct device_attribute *attr,
                            const char *buf, size_t count)
{
        unsigned int mode;

        if (!dev) {
                pr_debug("dev is null!!\n");
                return count;
        } else if (!kstrtouint(buf, 0, &mode)) {
                pr_debug("USB sib_enable: %d\n", mode);
                usb_phy_sib_enable_switch(mode);
        }
        return count;
}
#endif

#ifdef NEVER
#ifdef CONFIG_FPGA_EARLY_PORTING
static struct i2c_client *usb_i2c_client;
static const struct i2c_device_id usb_i2c_id[] = { {"mtk-usb", 0}, {} };

static struct i2c_board_info usb_i2c_dev __initdata = { I2C_BOARD_INFO("mtk-usb", 0x60) };


void USB_PHY_Write_Register8(UINT8 var, UINT8 addr)
{
        char buffer[2];

        buffer[0] = addr;
        buffer[1] = var;
        i2c_master_send(usb_i2c_client, &buffer, 2);
}

UINT8 USB_PHY_Read_Register8(UINT8 addr)
{
        UINT8 var;

        i2c_master_send(usb_i2c_client, &addr, 1);
        i2c_master_recv(usb_i2c_client, &var, 1);
        return var;
}

static int usb_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

        pr_debug("[MUSB]usb_i2c_probe, start\n");

        usb_i2c_client = client;

        /* disable usb mac suspend */
        DRV_WriteReg8(USB_SIF_BASE + 0x86a, 0x00);

        /* usb phy initial sequence */
        USB_PHY_Write_Register8(0x00, 0xFF);
        USB_PHY_Write_Register8(0x04, 0x61);
        USB_PHY_Write_Register8(0x00, 0x68);
        USB_PHY_Write_Register8(0x00, 0x6a);
        USB_PHY_Write_Register8(0x6e, 0x00);
        USB_PHY_Write_Register8(0x0c, 0x1b);
        USB_PHY_Write_Register8(0x44, 0x08);
        USB_PHY_Write_Register8(0x55, 0x11);
        USB_PHY_Write_Register8(0x68, 0x1a);


        pr_debug("[MUSB]addr: 0xFF, value: %x\n", USB_PHY_Read_Register8(0xFF));
        pr_debug("[MUSB]addr: 0x61, value: %x\n", USB_PHY_Read_Register8(0x61));
        pr_debug("[MUSB]addr: 0x68, value: %x\n", USB_PHY_Read_Register8(0x68));
        pr_debug("[MUSB]addr: 0x6a, value: %x\n", USB_PHY_Read_Register8(0x6a));
        pr_debug("[MUSB]addr: 0x00, value: %x\n", USB_PHY_Read_Register8(0x00));
        pr_debug("[MUSB]addr: 0x1b, value: %x\n", USB_PHY_Read_Register8(0x1b));
        pr_debug("[MUSB]addr: 0x08, value: %x\n", USB_PHY_Read_Register8(0x08));
        pr_debug("[MUSB]addr: 0x11, value: %x\n", USB_PHY_Read_Register8(0x11));
        pr_debug("[MUSB]addr: 0x1a, value: %x\n", USB_PHY_Read_Register8(0x1a));


        pr_debug("[MUSB]usb_i2c_probe, end\n");
        return 0;

}

static int usb_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info)
{
        strcpy(info->type, "mtk-usb");
        return 0;
}

static int usb_i2c_remove(struct i2c_client *client)
{
        return 0;
}


struct i2c_driver usb_i2c_driver = {
        .probe = usb_i2c_probe,
        .remove = usb_i2c_remove,
        .detect = usb_i2c_detect,
        .driver = {
                   .name = "mtk-usb",
                   },
        .id_table = usb_i2c_id,
};

int add_usb_i2c_driver(void)
{
        int ret = 0;

        i2c_register_board_info(0, &usb_i2c_dev, 1);
        if (i2c_add_driver(&usb_i2c_driver) != 0) {
                pr_debug("[MUSB]usb_i2c_driver initialization failed!!\n");
                ret = -1;
        } else
                pr_debug("[MUSB]usb_i2c_driver initialization succeed!!\n");

        return ret;
}
#endif				/* End of CONFIG_FPGA_EARLY_PORTING */
#endif				/* NEVER */
