/**
 * @file ui.cpp
 * @brief Screen layout, touch handling and the arm/throttle state machines.
 *
 * Each screen region caches the values it last displayed and skips drawing
 * entirely when nothing changed, so a steady screen costs almost no SPI
 * traffic. Any state that affects what a region renders must therefore appear
 * in that region's cache key, or the region will not repaint when it changes.
 */

#include "ui.h"
#include "ui_am32.h"
#include "ui_lcars.h"
#include "gfx.h"
#include "cst816.h"
#include "st7789.h"
#include "esc_task.h"
#include "board_pins.h"
#include "config.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// The screen layout is portrait-only: every region below is a fixed pixel row
// in a 240x320 frame. Setting LCD_ROTATION to 1 or 3 gives a 320x240 frame, in
// which the buttons fall off the bottom. That used to compile happily and fail
// silently on the panel; now it does not compile.
static_assert(GFX_W == 240 && GFX_H == 320,
              "the UI layout assumes portrait; LCD_ROTATION must be 0 or 2");

/**
 * @defgroup ui_layout Layout (240x320 portrait)
 * @brief Vertical extents of each screen region, in framebuffer rows.
 * @{
 */
#define Z_STATUS_Y0   0
#define Z_STATUS_Y1   25
#define Z_RPM_Y0      27
#define Z_RPM_Y1      126
#define Z_TELE_Y0     128
#define Z_TELE_Y1     232
#define Z_THR_Y0      234
#define Z_THR_Y1      277
#define Z_BTN_Y0      279
#define Z_BTN_Y1      319

#define THR_TRACK_X   8
#define THR_TRACK_Y   248
#define THR_TRACK_W   224
#define THR_TRACK_H   26
#define THR_TOUCH_PAD 8       // vertical grab slop around the track

// The number-display area doubles as a large relative throttle pad. It runs
// from the top of the RPM readout down to where the gauge's grab area starts,
// so the two regions abut exactly: no overlap, no dead strip between them.
#define PAD_Y0        Z_RPM_Y0
#define PAD_Y1        (THR_TRACK_Y - THR_TOUCH_PAD - 1)
#define PAD_H         (PAD_Y1 - PAD_Y0 + 1)

/** @} */

/** @brief A rectangular touch target with its own draw helper. */
struct Btn { int16_t x, y, w, h; };

static const Btn BTN_ARM  = { 6, 283, 104, 33 };
static const Btn BTN_HOLD = { 116, 283, 54, 33 };
static const Btn BTN_CFG  = { 176, 283, 58, 33 };

// config screen
/**
 * @defgroup ui_cfg_layout Settings screen layout
 * @brief Row positions, with the gaps asserted rather than eyeballed.
 * @{
 */
#define CFG_POLES_Y   72   /**< Pole-count -/+ row. */
#define CFG_MAXT_Y   148   /**< Throttle-ceiling -/+ row. */
#define CFG_CMD_Y    200   /**< EDT / BEEP row. */
#define CFG_ROW_H     38
#define CFG_HINT_Y   244   /**< Caption under the command buttons. */
#define CFG_AM32_Y   256   /**< AM32 config entry. */
#define CFG_BACK_Y   300
/** @} */

static const Btn BTN_POLES_M = { 14, CFG_POLES_Y, 46, 40 };
static const Btn BTN_POLES_P = { 180, CFG_POLES_Y, 46, 40 };
static const Btn BTN_MAXT_M  = { 14, CFG_MAXT_Y, 46, 40 };
static const Btn BTN_MAXT_P  = { 180, CFG_MAXT_Y, 46, 40 };
static const Btn BTN_EDT     = { 14, CFG_CMD_Y, 100, CFG_ROW_H };
static const Btn BTN_BEEP    = { 126, CFG_CMD_Y, 100, CFG_ROW_H };
static const Btn BTN_AM32    = { 14, CFG_AM32_Y, 212, 38 };
static const Btn BTN_BACK    = { 14, CFG_BACK_Y, 212, 18 };

// Caught by a screenshot rather than by reading the code: the caption used to
// sit at y=240, inside the 208..248 band the EDT and BEEP buttons occupy, and
// was drawn straight through them. Assert the gaps so it cannot recur.
static_assert(CFG_HINT_Y >= CFG_CMD_Y + CFG_ROW_H,
              "settings caption overlaps the EDT/BEEP buttons");
static_assert(CFG_AM32_Y >= CFG_HINT_Y + 7,
              "AM32 button overlaps the caption");
static_assert(CFG_BACK_Y >= CFG_AM32_Y + 38,
              "BACK button overlaps the AM32 button");
static_assert(CFG_BACK_Y + 18 <= GFX_H, "BACK button runs off the panel");

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------
static bool     s_armed = false;
static bool     s_hold = false;
static bool     s_config = false;
static bool     s_am32 = false;   /**< AM32 config mode owns the screen. */
static uint16_t s_throttle = 0;
static uint16_t s_maxThrottle = DEFAULT_MAX_THROTTLE;
static uint8_t  s_poles = DEFAULT_MOTOR_POLES;

static bool     s_armPressing = false;
static uint32_t s_armPressStart = 0;
static uint32_t s_zeroSince = 0;
static uint32_t s_lastTouchMs = 0;
static bool     s_dragging = false;
// anchor for HOLD-mode relative dragging on the gauge
static int16_t  s_dragAnchorX = 0;
static uint16_t s_dragAnchorThrottle = 0;

// relative throttle pad (the number-display area)
static bool     s_padDragging = false;
static bool     s_padEngaged = false;    // deadzone cleared
static int16_t  s_padTouchY = 0;         // where the finger landed
static int16_t  s_padAnchorPos = 0;      // negated y, so bigger = higher up
static uint16_t s_padAnchorThrottle = 0;

static TouchState s_touch;
static EscTelemetry s_tel;
static float s_batteryV = 0.0f;

/**
 * @brief Last-rendered values, so a region is only repainted when one changes.
 *
 * Several regions display overlapping state, so they keep separate copies —
 * `armed` and `btnArmed` for instance — because whichever region draws first
 * would otherwise consume the change and leave the others stale.
 */
static struct {
	int  armed, btnArmed, holdOn, armProgress;
	int  battMv;
	uint32_t rpm, erpm;
	int  linkAlive, rpmArmed;
	int  volts10, amps10, tempC, stress, status, rate, errPct, edt;
	int  throttleRaw, maxPct, thrArmed, thrHold;
	int  config, poles;
} s_shown;

/** @brief Force every region to repaint on the next uiTick(). */
static void invalidateAll() { memset(&s_shown, 0xFF, sizeof(s_shown)); }

/** @brief Point-in-button test. @return true if (@p x, @p y) is inside @p b. */
static bool hit(const Btn &b, int x, int y) {
	return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

/** @brief Draw a rounded button with its label centred. */
static void drawBtn(const Btn &b, const char *label, uint16_t fill,
                    uint16_t fg, int scale) {
#if UI_THEME == 1
	lcarsBtn(b.x, b.y, b.w, b.h, label, fill, fg, scale);
#else
	gfxRoundRect(b.x, b.y, b.w, b.h, 6, fill);
	gfxRoundFrame(b.x, b.y, b.w, b.h, 6, C_GRID);
	int tw = gfxTextW(label, scale);
	gfxText(b.x + (b.w - tw) / 2, b.y + (b.h - 7 * scale) / 2, label, fg, scale);
#endif
}

/** @brief Draw one telemetry tile: dim caption above a larger value. */
static void drawLabelled(int x, int y, int w, int h, const char *label,
                         const char *value, uint16_t vcol) {
#if UI_THEME == 1
	lcarsLabelled(x, y, w, h, label, value, vcol);
#else
	gfxRect(x, y, w, h, C_PANEL);
	gfxText(x + 6, y + 4, label, C_DIM, 1);
	gfxText(x + 6, y + 15, value, vcol, 2);
#endif
}

/**
 * @brief Move the throttle by how far the finger has travelled from its anchor.
 *
 * Re-anchoring at the rails is what stops the classic relative-drag failure:
 * swipe hard past an end and, without it, the anchor keeps accumulating travel
 * the throttle cannot follow, so reversing direction does nothing until all of
 * that phantom travel has been unwound.
 *
 * @param         nowPos    Current finger position along the control's axis.
 *                          Negate screen Y so that larger means further up.
 * @param[in,out] anchorPos Position the drag is measured from. Reset at a rail.
 * @param[in,out] anchorThr Throttle the drag is measured from. Reset at a rail.
 * @param         spanPx    Travel covering the whole ceiling at 100 % sensitivity.
 * @param         sensPct   Sensitivity percentage.
 */
static void relativeThrottle(int nowPos, int16_t *anchorPos,
                             uint16_t *anchorThr, int spanPx, int sensPct) {
	int32_t delta = (int32_t)(nowPos - *anchorPos) * (int32_t)s_maxThrottle / spanPx;
	delta = delta * sensPct / 100;
	int32_t t = (int32_t)(*anchorThr) + delta;

	if (t <= 0) {
		t = 0;
		*anchorPos = (int16_t)nowPos;
		*anchorThr = 0;
	} else if (t >= (int32_t)s_maxThrottle) {
		t = s_maxThrottle;
		*anchorPos = (int16_t)nowPos;
		*anchorThr = s_maxThrottle;
	}
	s_throttle = (uint16_t)t;
}

/** @brief True if an eRPM frame has arrived in the last 500 ms. */
static bool telemetryAlive() {
	return s_tel.lastRpmMs != 0 && (uint32_t)(millis() - s_tel.lastRpmMs) < 500;
}

/**
 * @defgroup ui_regions Screen regions
 * @brief Each returns immediately if its cached values are unchanged.
 * @{
 */

/** @brief Arm badge, DShot rate, EDT indicator and pack voltage. */
static void drawStatusBar() {
	int progress = 0;
	if (s_armPressing) {
		uint32_t held = millis() - s_armPressStart;
		progress = (int)(held * 100 / ARM_HOLD_MS);
		if (progress > 100) progress = 100;
	}
	int battMv = (int)(s_batteryV * 100.0f);

	if (s_shown.armed == (int)s_armed && s_shown.armProgress == progress &&
	    s_shown.battMv == battMv && s_shown.edt == (int)escEdtRequested())
		return;

	s_shown.armed = s_armed;
	s_shown.armProgress = progress;
	s_shown.battMv = battMv;
	s_shown.edt = escEdtRequested();

	gfxRect(0, Z_STATUS_Y0, GFX_W, Z_STATUS_Y1 - Z_STATUS_Y0 + 1, C_PANEL);

#if UI_THEME == 1
	uint16_t badge = s_armed ? LC_RED : LC_GREEN;
	gfxRoundRect(4, 3, 92, 20, 10, badge);
	const char *txt = s_armed ? "ARMED" : "SAFE";
	gfxText(4 + (92 - gfxTextW(txt, 2)) / 2, 7, txt, LC_BG, 2);

	if (!s_armed && progress > 0) {
		gfxRect(4, 21, 92 * progress / 100, 2, LC_ORANGE);
	}
#else
	uint16_t badge = s_armed ? C_RED : C_GREEN;
	gfxRoundRect(4, 3, 92, 20, 4, badge);
	const char *txt = s_armed ? "ARMED" : "SAFE";
	gfxText(4 + (92 - gfxTextW(txt, 2)) / 2, 7, txt, C_WHITE, 2);

	if (!s_armed && progress > 0) {
		gfxRect(4, 21, 92 * progress / 100, 2, C_LIME);
	}
#endif

	char buf[24];
	snprintf(buf, sizeof(buf), "DS%d", DSHOT_SPEED_KBAUD);
	gfxText(104, 9, buf, escEdtRequested() ? C_CYAN : C_DIM, 1);

	snprintf(buf, sizeof(buf), "%d.%02dV", (int)s_batteryV,
	         (int)((s_batteryV - (int)s_batteryV) * 100.0f + 0.5f));
	gfxText(GFX_W - 4 - gfxTextW(buf, 2), 7, buf, C_TEXT, 2);
}

/** @brief Big seven-segment RPM readout, eRPM line and the pad affordance. */
static void drawRpm() {
	bool alive = telemetryAlive();
	uint32_t rpm = alive ? s_tel.rpm : 0;
	uint32_t erpm = alive ? s_tel.erpm : 0;
	if (rpm > 99999) rpm = 99999;

	if (s_shown.rpm == rpm && s_shown.erpm == erpm &&
	    s_shown.linkAlive == (int)alive && s_shown.rpmArmed == (int)s_armed)
		return;
	s_shown.rpm = rpm;
	s_shown.erpm = erpm;
	s_shown.linkAlive = alive;
	s_shown.rpmArmed = s_armed;

	gfxRect(0, Z_RPM_Y0, GFX_W, Z_RPM_Y1 - Z_RPM_Y0 + 1, C_BG);

#if UI_THEME == 1
	uint16_t on = alive ? LC_ORANGE : C_REDDARK;
	gfxSegNumber(226, 32, 36, 62, 8, 6, rpm, 5, on, 0x1082);
	gfxText(226 - gfxTextW("RPM", 2), 100, "RPM", LC_DIM, 2);
#else
	uint16_t on = alive ? C_LIME : C_REDDARK;
	gfxSegNumber(226, 32, 36, 62, 8, 6, rpm, 5, on, 0x1082);
	gfxText(226 - gfxTextW("RPM", 2), 100, "RPM", C_DIM, 2);
#endif

	char buf[32];
	if (alive) snprintf(buf, sizeof(buf), "ERPM %lu", (unsigned long)erpm);
	else       snprintf(buf, sizeof(buf), "NO TELEMETRY");
	gfxText(10, 102, buf, alive ? C_DIM : C_AMBER, 1);

	snprintf(buf, sizeof(buf), "%dP", s_poles);
	gfxText(10, 114, buf, C_DIM, 1);

	// Tell the user this whole region is a throttle pad -- a relative control
	// with no visible handle is invisible otherwise.
	const char *hint = "SWIPE = THROTTLE";
	gfxText(226 - gfxTextW(hint, 1), 114, hint, s_armed ? C_CYAN : C_GRID, 1);
}

/** @brief Six EDT tiles: voltage, current, temperature, stress, status, link. */
static void drawTelemetry() {
	int volts10 = (int)(s_tel.volts * 10.0f + 0.5f);
	int amps10  = (int)(s_tel.amps * 10.0f + 0.5f);
	int status  = s_tel.alert ? 3 : s_tel.error ? 2 : s_tel.warning ? 1 : 0;

	if (s_shown.volts10 == volts10 && s_shown.amps10 == amps10 &&
	    s_shown.tempC == s_tel.tempC && s_shown.stress == s_tel.stress &&
	    s_shown.status == status && s_shown.rate == s_tel.packetRate &&
	    s_shown.errPct == s_tel.errPercent)
		return;

	s_shown.volts10 = volts10;
	s_shown.amps10  = amps10;
	s_shown.tempC   = s_tel.tempC;
	s_shown.stress  = s_tel.stress;
	s_shown.status  = status;
	s_shown.rate    = s_tel.packetRate;
	s_shown.errPct  = s_tel.errPercent;

	gfxRect(0, Z_TELE_Y0, GFX_W, Z_TELE_Y1 - Z_TELE_Y0 + 1, C_BG);

	const int cw = 118, ch = 33;
	const int cx[2] = {1, 121};
	const int cy[3] = {129, 164, 199};
	char buf[24];

	if (s_tel.haveVolts) snprintf(buf, sizeof(buf), "%d.%dV", volts10 / 10, volts10 % 10);
	else                 snprintf(buf, sizeof(buf), "--");
	drawLabelled(cx[0], cy[0], cw, ch, "VOLTAGE", buf, C_TEXT);

	if (s_tel.haveAmps) snprintf(buf, sizeof(buf), "%d.%dA", amps10 / 10, amps10 % 10);
	else                snprintf(buf, sizeof(buf), "--");
	drawLabelled(cx[1], cy[0], cw, ch, "CURRENT", buf, C_TEXT);

	if (s_tel.haveTemp) snprintf(buf, sizeof(buf), "%d`C", s_tel.tempC);
	else                snprintf(buf, sizeof(buf), "--");
	// backtick renders as a degree sign in the 5x7 font
	drawLabelled(cx[0], cy[1], cw, ch, "ESC TEMP", buf,
	             (s_tel.haveTemp && s_tel.tempC >= 90) ? C_RED : C_TEXT);

	if (s_tel.haveStress) snprintf(buf, sizeof(buf), "%d", s_tel.stress);
	else                  snprintf(buf, sizeof(buf), "--");
	drawLabelled(cx[1], cy[1], cw, ch, "STRESS", buf,
	             (s_tel.stress > 200) ? C_AMBER : C_TEXT);

	static const char *STATUS_TXT[4] = {"OK", "WARN", "ERROR", "ALERT"};
	static const uint16_t STATUS_COL[4] = {C_LIME, C_AMBER, C_RED, C_MAGENTA};
	drawLabelled(cx[0], cy[2], cw, ch, "ESC STATUS", STATUS_TXT[status], STATUS_COL[status]);

	snprintf(buf, sizeof(buf), "%d/S", s_tel.packetRate);
	uint16_t linkCol = s_tel.errPercent > 5 ? C_AMBER : C_TEXT;
	drawLabelled(cx[1], cy[2], cw, ch, "LINK", buf, linkCol);
	// error rate rides on the label row so it cannot collide with the value
	snprintf(buf, sizeof(buf), "%d%% ERR", s_tel.errPercent);
	gfxText(cx[1] + cw - 6 - gfxTextW(buf, 1), cy[2] + 4, buf,
	        s_tel.errPercent > 5 ? C_AMBER : C_DIM, 1);
}

/** @brief Throttle gauge, ceiling caption and the HOLD-mode grab handle. */
static void drawThrottle() {
	int pct = (int)((uint32_t)s_throttle * 100 / 2000);
	int maxPct = (int)((uint32_t)s_maxThrottle * 100 / 2000);

	if (s_shown.throttleRaw == (int)s_throttle && s_shown.maxPct == maxPct &&
	    s_shown.thrArmed == (int)s_armed && s_shown.thrHold == (int)s_hold)
		return;
	s_shown.throttleRaw = s_throttle;
	s_shown.maxPct = maxPct;
	s_shown.thrArmed = s_armed;
	s_shown.thrHold = s_hold;

	gfxRect(0, Z_THR_Y0, GFX_W, Z_THR_Y1 - Z_THR_Y0 + 1, C_BG);

	char buf[24];
#if UI_THEME == 1
	gfxText(THR_TRACK_X, 236, s_hold ? "THROTTLE REL" : "THROTTLE", LC_DIM, 1);
	snprintf(buf, sizeof(buf), "MAX %d%%", maxPct);
	gfxText(THR_TRACK_X + 80, 236, buf, LC_RED, 1);

	snprintf(buf, sizeof(buf), "%d%%", pct);
	gfxText(GFX_W - THR_TRACK_X - gfxTextW(buf, 2), 234, buf,
	        pct > 0 ? LC_ORANGE : LC_DIM, 2);

	gfxRoundRect(THR_TRACK_X, THR_TRACK_Y, THR_TRACK_W, THR_TRACK_H, 12, LC_DKBLUE);
	int fillW = s_maxThrottle
	                ? (int)((uint32_t)s_throttle * THR_TRACK_W / s_maxThrottle)
	                : 0;
	if (fillW > THR_TRACK_W) fillW = THR_TRACK_W;
	if (fillW > 0) {
		gfxRoundRect(THR_TRACK_X, THR_TRACK_Y, fillW, THR_TRACK_H, 12,
		             s_armed ? LC_ORANGE : LC_PURPLE);
	}
	if (s_armed && s_hold) {
		int hx = THR_TRACK_X + fillW - 3;
		if (hx < THR_TRACK_X) hx = THR_TRACK_X;
		if (hx > THR_TRACK_X + THR_TRACK_W - 6) hx = THR_TRACK_X + THR_TRACK_W - 6;
		gfxRect(hx, THR_TRACK_Y - 3, 6, THR_TRACK_H + 6, LC_WHITE);
	}
	gfxRoundFrame(THR_TRACK_X, THR_TRACK_Y, THR_TRACK_W, THR_TRACK_H, 12, LC_TAN);
	if (!s_armed) {
		gfxText(THR_TRACK_X + 8, THR_TRACK_Y + 10, "ARM TO ENABLE", LC_DIM, 1);
	}
#else
	gfxText(THR_TRACK_X, 236, s_hold ? "THROTTLE REL" : "THROTTLE", C_DIM, 1);
	snprintf(buf, sizeof(buf), "MAX %d%%", maxPct);
	gfxText(THR_TRACK_X + 80, 236, buf, C_AMBER, 1);

	// The full width of the track *is* the ceiling, so where you put your
	// finger is where the bar ends. The percentage is still of the real
	// 0..2000 DShot range so the number never lies about what the ESC sees.
	snprintf(buf, sizeof(buf), "%d%%", pct);
	gfxText(GFX_W - THR_TRACK_X - gfxTextW(buf, 2), 234, buf,
	        pct > 0 ? C_LIME : C_DIM, 2);

	gfxRoundRect(THR_TRACK_X, THR_TRACK_Y, THR_TRACK_W, THR_TRACK_H, 5, C_PANEL);
	int fillW = s_maxThrottle
	                ? (int)((uint32_t)s_throttle * THR_TRACK_W / s_maxThrottle)
	                : 0;
	if (fillW > THR_TRACK_W) fillW = THR_TRACK_W;
	if (fillW > 0) {
		gfxRoundRect(THR_TRACK_X, THR_TRACK_Y, fillW, THR_TRACK_H, 5,
		             s_armed ? C_LIME : C_GRID);
	}
	// In HOLD mode the bar is a relative control, so give it a grab handle to
	// say so -- it is a thing you push, not a place you tap.
	if (s_armed && s_hold) {
		int hx = THR_TRACK_X + fillW - 3;
		if (hx < THR_TRACK_X) hx = THR_TRACK_X;
		if (hx > THR_TRACK_X + THR_TRACK_W - 6) hx = THR_TRACK_X + THR_TRACK_W - 6;
		gfxRect(hx, THR_TRACK_Y - 3, 6, THR_TRACK_H + 6, C_WHITE);
	}

	gfxRoundFrame(THR_TRACK_X, THR_TRACK_Y, THR_TRACK_W, THR_TRACK_H, 5, C_GRID);
	if (!s_armed) {
		gfxText(THR_TRACK_X + 8, THR_TRACK_Y + 10, "ARM TO ENABLE", C_DIM, 1);
	}
#endif
}

/** @brief ARM/DISARM, HOLD and CFG buttons. */
static void drawButtons() {
	if (s_shown.holdOn == (int)s_hold && s_shown.btnArmed == (int)s_armed &&
	    s_shown.config == (int)s_config)
		return;
	s_shown.holdOn = s_hold;
	s_shown.btnArmed = s_armed;
	s_shown.config = s_config;

	gfxRect(0, Z_BTN_Y0, GFX_W, Z_BTN_Y1 - Z_BTN_Y0 + 1, C_BG);
#if UI_THEME == 1
	drawBtn(BTN_ARM, s_armed ? "DISARM" : "HOLD TO ARM",
	        s_armed ? LC_RED : LC_TAN, LC_BG, s_armed ? 2 : 1);
	drawBtn(BTN_HOLD, "HOLD", s_hold ? LC_BLUE : LC_PURPLE,
	        s_hold ? LC_WHITE : LC_BG, 1);
	drawBtn(BTN_CFG, "CFG", LC_PURPLE, LC_BG, 1);
#else
	drawBtn(BTN_ARM, s_armed ? "DISARM" : "HOLD TO ARM",
	        s_armed ? C_RED : C_PANEL, C_WHITE, s_armed ? 2 : 1);
	drawBtn(BTN_HOLD, "HOLD", s_hold ? C_BLUE : C_PANEL,
	        s_hold ? C_WHITE : C_DIM, 1);
	drawBtn(BTN_CFG, "CFG", C_PANEL, C_DIM, 1);
#endif
}

/** @} */

/** @brief Full-screen settings overlay. */
static void drawConfig() {
	if (s_shown.config == 1 && s_shown.poles == s_poles &&
	    s_shown.maxPct == (int)((uint32_t)s_maxThrottle * 100 / 2000))
		return;
	s_shown.config = 1;
	s_shown.poles = s_poles;
	s_shown.maxPct = (int)((uint32_t)s_maxThrottle * 100 / 2000);

	gfxFill(C_BG);
	gfxRect(0, 0, GFX_W, 26, C_PANEL);
	gfxText(8, 9, "SETTINGS", C_TEXT, 2);

	char buf[24];

	gfxText(14, CFG_POLES_Y - 20, "MOTOR POLES", C_DIM, 1);
	drawBtn(BTN_POLES_M, "-", C_PANEL, C_TEXT, 2);
	drawBtn(BTN_POLES_P, "+", C_PANEL, C_TEXT, 2);
	snprintf(buf, sizeof(buf), "%d", s_poles);
	gfxText(120 - gfxTextW(buf, 3) / 2, 82, buf, C_TEXT, 3);

	gfxText(14, CFG_MAXT_Y - 20, "THROTTLE CEILING", C_DIM, 1);
	drawBtn(BTN_MAXT_M, "-", C_PANEL, C_TEXT, 2);
	drawBtn(BTN_MAXT_P, "+", C_PANEL, C_TEXT, 2);
	snprintf(buf, sizeof(buf), "%d%%", (int)((uint32_t)s_maxThrottle * 100 / 2000));
	gfxText(120 - gfxTextW(buf, 3) / 2, 158, buf, C_AMBER, 3);

	drawBtn(BTN_EDT, "EDT ON", C_PANEL, C_CYAN, 1);
	drawBtn(BTN_BEEP, "BEEP", C_PANEL, C_CYAN, 1);
	gfxTextCenter(CFG_HINT_Y, "COMMANDS NEED THE ESC DISARMED", C_DIM, 1);

	drawBtn(BTN_AM32, "AM32 ESC CONFIG", C_PANEL, C_CYAN, 2);
	drawBtn(BTN_BACK, "BACK", C_PANEL, C_TEXT, 1);
}

/** @brief Dispatch a press on the settings overlay. */
static void handleConfigTouch() {
	if (!s_touch.pressed) return;
	int x = s_touch.x, y = s_touch.y;

	if (hit(BTN_POLES_M, x, y) && s_poles > MIN_MOTOR_POLES) {
		s_poles -= 2;
		escSetPoles(s_poles);
		s_shown.poles = -1;
	} else if (hit(BTN_POLES_P, x, y) && s_poles < MAX_MOTOR_POLES) {
		s_poles += 2;
		escSetPoles(s_poles);
		s_shown.poles = -1;
	} else if (hit(BTN_MAXT_M, x, y) && s_maxThrottle > MAX_THROTTLE_STEP) {
		s_maxThrottle -= MAX_THROTTLE_STEP;
		s_shown.maxPct = -1;
	} else if (hit(BTN_MAXT_P, x, y) && s_maxThrottle < MAX_THROTTLE_CEILING) {
		s_maxThrottle += MAX_THROTTLE_STEP;
		s_shown.maxPct = -1;
	} else if (hit(BTN_EDT, x, y)) {
		escRequestEdtEnable();
	} else if (hit(BTN_BEEP, x, y)) {
		escRequestBeep(1);
	} else if (hit(BTN_AM32, x, y)) {
		s_am32 = true;
		gfxFill(C_BG);
		uiAm32Enter();
	} else if (hit(BTN_BACK, x, y)) {
		s_config = false;
		invalidateAll();
		gfxFill(C_BG);
	}
}

/**
 * @brief Main-screen input: throttle gauge, throttle pad and the buttons.
 *
 * Two throttle surfaces feed the same value. The gauge is absolute in spring
 * mode and relative in HOLD mode; the pad is always relative, because a large
 * region with no positional meaning must not move the motor on a bare tap.
 */
static void handleMainTouch() {
	int x = s_touch.x, y = s_touch.y;

	// --- throttle slider ---
	bool inTrack = (y >= THR_TRACK_Y - THR_TOUCH_PAD) &&
	               (y < THR_TRACK_Y + THR_TRACK_H + THR_TOUCH_PAD) &&
	               (x >= THR_TRACK_X - 8) && (x < THR_TRACK_X + THR_TRACK_W + 8);

	bool inPad = (y >= PAD_Y0) && (y <= PAD_Y1);

	if (s_touch.pressed && s_armed) {
		if (inTrack) {
			s_dragging = true;
			s_dragAnchorX = (int16_t)x;
			s_dragAnchorThrottle = s_throttle;
		} else if (inPad) {
			s_padDragging = true;
			s_padEngaged = false;
			s_padTouchY = (int16_t)y;
		}
	}
	if (!s_touch.down) {
		s_dragging = false;
		s_padDragging = false;
		s_padEngaged = false;
	}

	if (s_dragging && s_armed) {
		if (s_hold) {
			// RELATIVE. The throttle is latched in HOLD mode, so an absolute
			// slider would make the motor jump to wherever the finger landed.
			relativeThrottle(x, &s_dragAnchorX, &s_dragAnchorThrottle,
			                 THR_TRACK_W, HOLD_DRAG_SENSITIVITY_PCT);
		} else {
			// ABSOLUTE. Spring-loaded trigger: finger position is the throttle,
			// and it returns to zero the moment you let go.
			int rel = x - THR_TRACK_X;
			if (rel < 0) rel = 0;
			if (rel > THR_TRACK_W) rel = THR_TRACK_W;
			s_throttle = (uint16_t)((uint32_t)rel * s_maxThrottle / THR_TRACK_W);
		}
	} else if (s_padDragging && s_armed) {
		// Always relative, in both modes: the number area has no positional
		// meaning, so a tap on it must never move the motor. Position is
		// negated so that swiping *up* the screen raises the throttle.
		if (!s_padEngaged) {
			int travel = y - s_padTouchY;
			if (travel < 0) travel = -travel;
			if (travel >= PAD_DEADZONE_PX) {
				s_padEngaged = true;
				s_padAnchorPos = (int16_t)(-y);
				s_padAnchorThrottle = s_throttle;
			}
		}
		if (s_padEngaged) {
			relativeThrottle(-y, &s_padAnchorPos, &s_padAnchorThrottle,
			                 PAD_H, PAD_DRAG_SENSITIVITY_PCT);
		}
	} else if (!s_hold) {
		s_throttle = 0;
	}
	if (!s_armed) s_throttle = 0;

	// --- arm button: press and hold ---
	if (s_touch.down && hit(BTN_ARM, x, y) && hit(BTN_ARM, s_touch.downX, s_touch.downY)) {
		if (s_armed) {
			// disarm is instant, on press
			if (s_touch.pressed) {
				s_armed = false;
				s_throttle = 0;
				s_hold = false;
				escSetArmed(false);
				invalidateAll();
			}
		} else {
			if (!s_armPressing) {
				s_armPressing = true;
				s_armPressStart = millis();
			}
			bool zeroLongEnough =
			    (uint32_t)(millis() - s_zeroSince) >= ARM_ZERO_THROTTLE_MS;
			if (zeroLongEnough &&
			    (uint32_t)(millis() - s_armPressStart) >= ARM_HOLD_MS) {
				s_armed = true;
				s_throttle = 0;
				escSetArmed(true);
				s_armPressing = false;
				invalidateAll();
			}
		}
	} else {
		s_armPressing = false;
	}

	if (s_touch.pressed) {
		if (hit(BTN_HOLD, x, y)) {
			s_hold = !s_hold;
			if (!s_hold) s_throttle = 0;
		} else if (hit(BTN_CFG, x, y)) {
			s_config = true;
			s_armed = false;
			s_throttle = 0;
			s_hold = false;
			escSetArmed(false);
			invalidateAll();
			s_shown.config = -1;
		}
	}
}

void uiDrawSplash() {
#if UI_THEME == 1
	lcarsDrawSplash();
#else
	gfxFill(C_BG);
	gfxTextCenter(104, "DSHOT", C_LIME, 4);
	gfxTextCenter(144, "DISPLAY", C_TEXT, 4);
	gfxTextCenter(188, "BIDIRECTIONAL ESC TESTER", C_DIM, 1);
	gfxTextCenter(216, "JuWi made", C_CYAN, 2);
#endif
}

uint16_t uiThrottle() { return s_throttle; }
bool uiArmed() { return s_armed; }

void uiInit() {
	memset(&s_touch, 0, sizeof(s_touch));
	memset(&s_tel, 0, sizeof(s_tel));
	invalidateAll();

	analogReadResolution(12);

	escSetPoles(s_poles);
	escSetArmed(false);

	gfxFill(C_BG);
	s_zeroSince = millis();
	s_lastTouchMs = millis();
}

void uiTick() {
	escHeartbeat();
	escSnapshot(&s_tel);
	touchPoll(&s_touch);

	// battery, lightly smoothed
	int raw = analogRead(BAT_ADC_CHAN);
	float v = (raw * 3.3f / 4095.0f) * BAT_DIVIDER;
	s_batteryV = s_batteryV == 0.0f ? v : (s_batteryV * 0.9f + v * 0.1f);

	if (s_touch.down) s_lastTouchMs = millis();

	// AM32 config owns the whole screen and the signal pin while it runs.
	if (s_am32) {
		if (!uiAm32Tick(&s_touch)) {
			s_am32 = false;
			invalidateAll();
			gfxFill(C_BG);
			s_shown.config = -1;
		}
		st7789FlushDirty();
		return;
	}

	// s_zeroSince tracks the last moment throttle was non-zero, so
	// (millis() - s_zeroSince) is "how long we have been at idle".
	if (s_throttle != 0) s_zeroSince = millis();

	if (s_config) {
		handleConfigTouch();
		drawConfig();
	} else {
		handleMainTouch();

		// idle auto-disarm
#if IDLE_DISARM_MS > 0
		if (s_armed && (uint32_t)(millis() - s_lastTouchMs) > IDLE_DISARM_MS) {
			s_armed = false;
			s_throttle = 0;
			s_hold = false;
			escSetArmed(false);
			invalidateAll();
		}
#endif

		escSetThrottle(s_throttle);

		drawStatusBar();
		drawRpm();
		drawTelemetry();
		drawThrottle();
		drawButtons();
	}

	st7789FlushDirty();
}
