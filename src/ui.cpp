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
#include "ui_setup.h"
#include "ui_input.h"
#include "settings.h"
#include "gfx.h"
#include "touch.h"
#include "st7789.h"
#include "esc_task.h"
#include "esc_merge.h"
#include "sd_log.h"
#include "board_desc.h"
#include "config.h"

#include "plat.h"
#include <hardware/adc.h>
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
#define CFG_CMD_Y    200   /**< BEEP row. */
#define CFG_ROW_H     38
#define CFG_HINT_Y   244   /**< Caption under the command buttons. */
#define CFG_AM32_Y   256   /**< AM32 config entry. */
#define CFG_BACK_Y   300
/** @} */

static const Btn BTN_POLES_M = { 14, CFG_POLES_Y, 46, 40 };
static const Btn BTN_POLES_P = { 180, CFG_POLES_Y, 46, 40 };
static const Btn BTN_MAXT_M  = { 14, CFG_MAXT_Y, 46, 40 };
static const Btn BTN_MAXT_P  = { 180, CFG_MAXT_Y, 46, 40 };
// BEEP has the row to itself. The EDT enable used to sit beside it and is
// gone: the firmware now sends it whenever an ESC starts answering, so the
// button was a control for something already handled. What is left of EDT here
// is the read-only chip in the title bar.
static const Btn BTN_BEEP    = { 14, CFG_CMD_Y, 212, CFG_ROW_H };
// One row, three destinations. The settings screen was already full to the
// bottom edge before SETUP needed a home, so the row splits rather than the
// screen growing -- 3 x 68 plus two 4 px gaps is exactly the 212 px between the
// margins, and the asserts below keep it that way.
#define CFG_NAV_W    68
#define CFG_NAV_GAP  4
static const Btn BTN_AM32  = { 14, CFG_AM32_Y, CFG_NAV_W, 38 };
static const Btn BTN_LOG   = { 14 + CFG_NAV_W + CFG_NAV_GAP, CFG_AM32_Y, CFG_NAV_W, 38 };
static const Btn BTN_SETUP = { 14 + 2 * (CFG_NAV_W + CFG_NAV_GAP), CFG_AM32_Y, CFG_NAV_W, 38 };

static_assert(14 + 3 * CFG_NAV_W + 2 * CFG_NAV_GAP <= 226,
              "the AM32/LOG/SETUP row is too wide");
static const Btn BTN_BACK    = { 14, CFG_BACK_Y, 212, 18 };

/**
 * @defgroup ui_log_layout Logging screen layout
 * @{
 */
#define LOG_ROW0_Y     40   /**< First status row. */
#define LOG_ROW_H      21
#define LOG_ROWS        9   /**< Through MOUNT; CARD and MOUNT are diagnostics. */
#define LOG_TOGGLE_Y  232   /**< START / STOP button. */
#define LOG_TOGGLE_H   34
#define LOG_RETRY_Y   270   /**< RETRY MOUNT. */
#define LOG_RETRY_H    22
#define LOG_BACK_Y    296
#define LOG_BACK_H     18
/** @} */

static const Btn BTN_LOG_TOGGLE = { 14, LOG_TOGGLE_Y, 212, LOG_TOGGLE_H };
static const Btn BTN_LOG_RETRY  = { 14, LOG_RETRY_Y, 212, LOG_RETRY_H };
static const Btn BTN_LOG_BACK   = { 14, LOG_BACK_Y, 212, LOG_BACK_H };

static_assert(LOG_ROW0_Y + LOG_ROWS * LOG_ROW_H <= LOG_TOGGLE_Y,
              "logging rows overlap the START/STOP button");
static_assert(LOG_TOGGLE_Y + LOG_TOGGLE_H <= LOG_RETRY_Y,
              "START/STOP button overlaps RETRY");
static_assert(LOG_RETRY_Y + LOG_RETRY_H <= LOG_BACK_Y,
              "RETRY overlaps BACK");
static_assert(LOG_BACK_Y + LOG_BACK_H <= GFX_H,
              "logging BACK runs off the panel");

// Caught by a screenshot rather than by reading the code: the caption used to
// sit at y=240, inside the 208..248 band the command row occupies, and
// was drawn straight through them. Assert the gaps so it cannot recur.
static_assert(CFG_HINT_Y >= CFG_CMD_Y + CFG_ROW_H,
              "settings caption overlaps the BEEP button");
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
static bool     s_setup = false;  /**< SETUP screen owns the screen. */
static bool     s_logScreen = false; /**< Logging status screen is up. */
static uint16_t s_throttle = 0;

// Pole count and throttle ceiling live in the persisted settings rather than in
// file statics here, so the -/+ buttons on the settings screen and the SAVE on
// the setup screen act on one value. These two read through to it.
#define s_maxThrottle (settings()->maxThrottle)
#define s_poles       (settings()->poles)

static bool     s_armPressing = false;
static uint32_t s_armPressStart = 0;
static uint32_t s_zeroSince = 0;
static uint32_t s_lastTouchMs = 0;
static bool     s_dragging = false;
// anchor for HOLD-mode relative dragging on the gauge
static int16_t  s_dragAnchorX = 0;
static uint16_t s_dragAnchorThrottle = 0;

// relative throttle pad (the number-display area)
/**
 * @brief Repeat state for the two steppers on the settings screen.
 *
 * One per control, not one shared: sliding a finger from the pole stepper onto
 * the ceiling stepper must not carry the first one's acceleration across.
 */
static Repeat s_polesRepeat;
static Repeat s_maxtRepeat;

static bool     s_padDragging = false;
static bool     s_padEngaged = false;    // deadzone cleared
static int16_t  s_padTouchY = 0;         // where the finger landed
static int16_t  s_padAnchorPos = 0;      // negated y, so bigger = higher up
static uint16_t s_padAnchorThrottle = 0;

/**
 * @defgroup ui_cmdflash Command button feedback
 *
 * EDT and BEEP fire one-shot DShot commands. The command itself is over in
 * 6-10 frames -- 10 ms at 1 kHz -- which at a 40 Hz repaint is invisible, so
 * the buttons appeared dead even though both were working. Worse, both are
 * refused outright while armed, and that refusal was silent too.
 *
 * So the press is latched briefly and the button is drawn
 * inverted for that long: accent-coloured when the command went out, red when
 * it was refused. Long enough to register, short enough not to lag the next
 * press.
 * @{
 */
/** @brief How long a press stays acknowledged on screen, in ms. */
#define CMD_FLASH_MS 350

/** @brief Which command button is currently flashing. */
enum class CmdFlash : uint8_t { None = 0, Beep };

static CmdFlash s_cmdFlash = CmdFlash::None;
static bool     s_cmdFlashOk = false;   /**< False when the ESC refused it. */
static uint32_t s_cmdFlashUntilMs = 0;

/** @brief True while a press is still being acknowledged on screen. */
static bool cmdFlashActive() {
	return s_cmdFlash != CmdFlash::None &&
	       (int32_t)(millis() - s_cmdFlashUntilMs) < 0;
}

/** @brief Latch a press for the UI to show. @param which Button. @param ok Accepted. */
static void cmdFlashSet(CmdFlash which, bool ok) {
	s_cmdFlash = which;
	s_cmdFlashOk = ok;
	s_cmdFlashUntilMs = millis() + CMD_FLASH_MS;
}
/** @} */

/**
 * @defgroup ui_blflash Arm feedback through the backlight
 *
 * Arming and disarming are the two most consequential things this UI does, and
 * both used to announce themselves only by a badge changing colour -- in a
 * corner of a screen you are not looking at, because you are looking at the
 * motor.
 *
 * Dipping the whole panel for a moment is a peripheral-vision event. It costs
 * one PWM write per frame, needs no hardware neither board has, and works
 * identically in both themes.
 * @{
 */
/** @brief How long the panel dims for on an arm-state change, in ms. */
#define BL_DIP_MS 120
/** @brief Fraction of the normal level to dip to, as a divisor. */
#define BL_DIP_DIV 4

static uint32_t s_blDipUntil = 0;

/** @brief Start a dip. Called on every arm and disarm, however it happened. */
static void backlightDip() { s_blDipUntil = millis() + BL_DIP_MS; }
/** @} */

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
	int  volts100, amps100, tempC, stress, status, rate, errPct, edt;
	int  voltsSrc, ampsSrc, mah;
	int  throttleRaw, maxPct, thrArmed, thrHold;
	int  config, poles;
	int  cmdFlash, edtActive, dirty, btnPress, idleLeft, padLive;
	int  logState, logFile, logDrops;
	uint32_t logBytes, logFrames, logPeak, logWorstMs;
} s_shown;

/** @brief Force every region to repaint on the next uiTick(). */
static void invalidateAll() { memset(&s_shown, 0xFF, sizeof(s_shown)); }

/** @brief Point-in-button test. @return true if (@p x, @p y) is inside @p b. */
static bool hit(const Btn &b, int x, int y) {
	return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

/**
 * @brief Draw a rounded button with its label centred.
 *
 * The pressed look is deliberately additive -- a brighter frame and the label
 * nudged a pixel down and right -- rather than swapping the fill and foreground.
 * Every button here picks its label colour to suit its own fill (white on red,
 * cyan on panel, background-coloured on a flash), so a state that replaces the
 * fill would have to know about all of those to stay legible. Adding to what is
 * already there cannot break any of those pairings, and reads on every one.
 *
 * @param b       Target rectangle.
 * @param label   Text, centred.
 * @param fill    Resting fill colour.
 * @param fg      Label colour, chosen against @p fill.
 * @param scale   Text scale.
 * @param pressed True while a finger is on it.
 */
static void drawBtn(const Btn &b, const char *label, uint16_t fill,
                    uint16_t fg, int scale, bool pressed = false) {
	gfxRoundRect(b.x, b.y, b.w, b.h, 6, fill);
	gfxRoundFrame(b.x, b.y, b.w, b.h, 6, pressed ? C_INK : C_GRID);
	if (pressed && b.w > 6 && b.h > 6)
		gfxRoundFrame(b.x + 2, b.y + 2, b.w - 4, b.h - 4, 4, C_INK);
	int tw = gfxTextW(label, scale);
	int dx = pressed ? 1 : 0;
	gfxText(b.x + dx + (b.w - tw) / 2, b.y + dx + (b.h - 7 * scale) / 2,
	        label, fg, scale);
}

/**
 * @brief True while @p b is being touched by a press that began inside it.
 *
 * Both halves matter. Without the current position a finger that slid off still
 * looks held; without the start position a finger that slid *on* from elsewhere
 * looks pressed and would fire on release.
 */
static bool pressing(const Btn &b, const TouchState &t) {
	return t.down && hit(b, t.x, t.y) && hit(b, t.downX, t.downY);
}

/**
 * @brief True on the frame a tap completes: released inside, started inside.
 *
 * This is the escape hatch. Firing on touch-down means a mis-tap has already
 * happened by the time you notice it; firing on release means sliding a finger
 * off the button cancels, which is what every touch UI does and therefore what
 * nobody has to be told.
 */
static bool tapped(const Btn &b, const TouchState &t) {
	return t.released && hit(b, t.x, t.y) && hit(b, t.downX, t.downY);
}

/** @brief Draw one telemetry tile: dim caption above a larger value. */
static void drawLabelled(int x, int y, int w, int h, const char *label,
                         const char *value, uint16_t vcol) {
	gfxRect(x, y, w, h, C_PANEL);
	gfxText(x + 6, y + 4, label, C_DIM, 1);
	gfxText(x + 6, y + 15, value, vcol, 2);
}

/**
 * @brief Right-aligned "KISS" / "EDT" tag on a tile's label row.
 *
 * Cyan for KISS, dim for EDT: the point is to make the *fine* source obvious at
 * a glance, so that a readout quietly dropping back to 0.25 V steps is visible
 * rather than something you notice later in a log.
 *
 * @param xRight Right edge of the tile.
 * @param y      Tile top.
 * @param src    Which source supplied the value.
 */
static void drawSourceTag(int xRight, int y, EscSource src) {
	if (src == EscSource::None) return;
	const char *s = escSourceLabel(src);
	gfxText(xRight - 6 - gfxTextW(s, 1), y + 4, s,
	        src == EscSource::Kiss ? C_CYAN : C_DIM, 1);
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

/** @brief True if an eRPM frame has arrived recently. @see ESC_LINK_STALE_MS */
static bool telemetryAlive() {
	return escFieldFresh(s_tel.lastRpmMs, millis(), ESC_LINK_STALE_MS);
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

	SdLogStatus log;
	sdLogStatus(&log);

	// Seconds left before the idle interlock disarms, or -1 when it is not
	// imminent. Counting down out loud matters: a motor stopping for no visible
	// reason is indistinguishable from a fault, and this interlock has no other
	// symptom at all.
	int idleLeft = -1;
#if IDLE_DISARM_MS > 0
	if (s_armed) {
		int32_t left = (int32_t)IDLE_DISARM_MS - (int32_t)(millis() - s_lastTouchMs);
		if (left < 0) left = 0;
		if (left <= 5000) idleLeft = (int)((left + 999) / 1000);
	}
#endif

	if (s_shown.armed == (int)s_armed && s_shown.armProgress == progress &&
	    s_shown.battMv == battMv && s_shown.edt == (int)escEdtRequested() &&
	    s_shown.logState == (int)log.state && s_shown.idleLeft == idleLeft)
		return;

	s_shown.idleLeft = idleLeft;
	s_shown.armed = s_armed;
	s_shown.armProgress = progress;
	s_shown.battMv = battMv;
	s_shown.edt = escEdtRequested();
	s_shown.logState = (int)log.state;

	gfxRect(0, Z_STATUS_Y0, GFX_W, Z_STATUS_Y1 - Z_STATUS_Y0 + 1, C_PANEL);

	uint16_t badge = s_armed ? C_RED : C_GREEN;
	gfxRoundRect(4, 3, 92, 20, 4, badge);
	const char *txt = s_armed ? "ARMED" : "SAFE";
	gfxText(4 + (92 - gfxTextW(txt, 2)) / 2, 7, txt, C_ONACCENT, 2);

	if (!s_armed && progress > 0) {
		gfxRect(4, 21, 92 * progress / 100, 2, C_LIME);
	}

	char buf[24];
	snprintf(buf, sizeof(buf), "DS%d", DSHOT_SPEED_KBAUD);
	gfxText(104, 3, buf, escEdtRequested() ? C_CYAN : C_DIM, 1);

	// Recording state, under the DShot rate. Only worth a glance -- the detail
	// lives on the LOG screen -- but "am I actually recording?" is the one
	// question you want answered without navigating anywhere.
	const char *lg;
	uint16_t lgCol;
	switch (log.state) {
		case SdLogState::Logging: lg = "REC";   lgCol = C_RED;   break;
		case SdLogState::Idle:    lg = "SD";    lgCol = C_DIM;   break;
		case SdLogState::Error:   lg = "SDERR"; lgCol = C_AMBER; break;
		default:                  lg = "NOSD";  lgCol = C_GRID;  break;
	}
	gfxText(104, 14, lg, lgCol, 1);

	if (idleLeft >= 0) {
		// Displaces the pack voltage rather than squeezing in beside it: for the
		// five seconds this is up it is the more urgent of the two, and there is
		// not room for both at a legible size.
		snprintf(buf, sizeof(buf), "IDLE %ds", idleLeft);
		gfxText(GFX_W - 4 - gfxTextW(buf, 2), 7, buf, C_AMBER, 2);
	} else {
		snprintf(buf, sizeof(buf), "%d.%02dV", (int)s_batteryV,
		         (int)((s_batteryV - (int)s_batteryV) * 100.0f + 0.5f));
		gfxText(GFX_W - 4 - gfxTextW(buf, 2), 7, buf, C_TEXT, 2);
	}
}

/** @brief Big seven-segment RPM readout, eRPM line and the pad affordance. */
static void drawRpm() {
	bool alive = telemetryAlive();
	uint32_t rpm = alive ? s_tel.rpm : 0;
	uint32_t erpm = alive ? s_tel.erpm : 0;
	if (rpm > 99999) rpm = 99999;

	if (s_shown.rpm == rpm && s_shown.erpm == erpm &&
	    s_shown.linkAlive == (int)alive && s_shown.rpmArmed == (int)s_armed &&
	    s_shown.padLive == (int)s_padEngaged)
		return;
	s_shown.padLive = s_padEngaged;
	s_shown.rpm = rpm;
	s_shown.erpm = erpm;
	s_shown.linkAlive = alive;
	s_shown.rpmArmed = s_armed;

	gfxRect(0, Z_RPM_Y0, GFX_W, Z_RPM_Y1 - Z_RPM_Y0 + 1, C_BG);

	uint16_t on = alive ? C_LIME : C_REDDARK;
	gfxSegNumber(226, 32, 36, 62, themeSegStroke(8), 6, rpm, 5, on, C_GHOST);

	gfxText(226 - gfxTextW("RPM", 2), 100, "RPM", C_DIM, 2);

	char buf[32];
	if (alive) snprintf(buf, sizeof(buf), "ERPM %lu", (unsigned long)erpm);
	else       snprintf(buf, sizeof(buf), "NO TELEMETRY");
	gfxText(10, 102, buf, alive ? C_DIM : C_AMBER, 1);

	snprintf(buf, sizeof(buf), "%dP", s_poles);
	gfxText(10, 114, buf, C_DIM, 1);

	// Tell the user this whole region is a throttle pad -- a relative control
	// with no visible handle is invisible otherwise.
	// Three states, not two. The pad has a deadzone, so there is a moment
	// between touching it and it taking effect, and the only other evidence a
	// drag engaged is the throttle number moving -- which is exactly what you
	// are not watching while a motor spins up.
	const char *hint = s_padEngaged ? "SWIPE ACTIVE" : "SWIPE = THROTTLE";
	gfxText(226 - gfxTextW(hint, 1), 114, hint,
	        s_padEngaged ? C_LIME : s_armed ? C_CYAN : C_GRID, 1);
}

/** @brief Six EDT tiles: voltage, current, temperature, stress, status, link. */
static void drawTelemetry() {
	// Merged view: KISS where it is fresh, EDT otherwise, decided per field.
	EscReading r;
	escMerge(&s_tel, millis(), KISS_STALE_MS, EDT_STALE_MS, &r);

	// Hundredths, not tenths. KISS resolves 0.01 V and 0.01 A, and rounding the
	// display to 0.1 would throw away exactly the precision the extra wire was
	// run for.
	int volts100 = (int)(r.volts * 100.0f + 0.5f);
	int amps100  = (int)(r.amps * 100.0f + 0.5f);
	// -1 is "no status block", not "OK". An expired status must not read as
	// all-clear -- that is the one direction a warning indicator may not fail.
	int status   = (r.statusFrom == EscSource::None) ? -1
	             : r.alert ? 3 : r.error ? 2 : r.warning ? 1 : 0;

	if (s_shown.volts100 == volts100 && s_shown.amps100 == amps100 &&
	    s_shown.tempC == r.tempC && s_shown.stress == (int)r.stress &&
	    s_shown.status == status && s_shown.rate == s_tel.packetRate &&
	    s_shown.errPct == s_tel.errPercent &&
	    s_shown.voltsSrc == (int)r.voltsFrom && s_shown.ampsSrc == (int)r.ampsFrom &&
	    s_shown.mah == (int)r.mah)
		return;

	s_shown.volts100 = volts100;
	s_shown.amps100  = amps100;
	s_shown.tempC    = r.tempC;
	s_shown.stress   = r.stress;
	s_shown.status   = status;
	s_shown.rate     = s_tel.packetRate;
	s_shown.errPct   = s_tel.errPercent;
	s_shown.voltsSrc = (int)r.voltsFrom;
	s_shown.ampsSrc  = (int)r.ampsFrom;
	s_shown.mah      = r.mah;

	gfxRect(0, Z_TELE_Y0, GFX_W, Z_TELE_Y1 - Z_TELE_Y0 + 1, C_BG);

	const int cw = 118, ch = 33;
	const int cx[2] = {1, 121};
	const int cy[3] = {129, 164, 199};
	char buf[24];

	// A number whose provenance changes silently is worse than no number:
	// 12.25 V from EDT means "somewhere in a 0.25 V bucket", the same reading
	// from KISS means "within 0.01 V". The tag says which claim is being made.
	if (r.voltsFrom != EscSource::None)
		snprintf(buf, sizeof(buf), "%d.%02dV", volts100 / 100, volts100 % 100);
	else
		snprintf(buf, sizeof(buf), "--");
	drawLabelled(cx[0], cy[0], cw, ch, "VOLTAGE", buf, C_TEXT);
	drawSourceTag(cx[0] + cw, cy[0], r.voltsFrom);

	if (r.ampsFrom != EscSource::None)
		snprintf(buf, sizeof(buf), "%d.%02dA", amps100 / 100, amps100 % 100);
	else
		snprintf(buf, sizeof(buf), "--");
	drawLabelled(cx[1], cy[0], cw, ch, "CURRENT", buf, C_TEXT);
	drawSourceTag(cx[1] + cw, cy[0], r.ampsFrom);

	if (r.tempFrom != EscSource::None) snprintf(buf, sizeof(buf), "%d`C", r.tempC);
	else                               snprintf(buf, sizeof(buf), "--");
	// backtick renders as a degree sign in the 5x7 font
	drawLabelled(cx[0], cy[1], cw, ch, "ESC TEMP", buf,
	             (r.tempFrom != EscSource::None && r.tempC >= 90) ? C_RED : C_TEXT);
	// Consumption has no EDT equivalent, so it only ever appears with KISS.
	if (r.mahFrom == EscSource::Kiss) {
		snprintf(buf, sizeof(buf), "%umAh", (unsigned)r.mah);
		gfxText(cx[0] + cw - 6 - gfxTextW(buf, 1), cy[1] + 4, buf, C_DIM, 1);
	}

	if (r.stressFrom != EscSource::None) snprintf(buf, sizeof(buf), "%d", r.stress);
	else                                 snprintf(buf, sizeof(buf), "--");
	drawLabelled(cx[1], cy[1], cw, ch, "STRESS", buf,
	             (r.stressFrom != EscSource::None && r.stress > 200) ? C_AMBER : C_TEXT);

	static const char *STATUS_TXT[4] = {"OK", "WARN", "ERROR", "ALERT"};
	static const uint16_t STATUS_COL[4] = {C_LIME, C_AMBER, C_RED, C_MAGENTA};
	drawLabelled(cx[0], cy[2], cw, ch, "ESC STATUS",
	             status < 0 ? "--" : STATUS_TXT[status],
	             status < 0 ? C_DIM : STATUS_COL[status]);

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
		gfxRect(hx, THR_TRACK_Y - 3, 6, THR_TRACK_H + 6, C_INK);
	}

	gfxRoundFrame(THR_TRACK_X, THR_TRACK_Y, THR_TRACK_W, THR_TRACK_H, 5, C_GRID);
	if (!s_armed) {
		gfxText(THR_TRACK_X + 8, THR_TRACK_Y + 10, "ARM TO ENABLE", C_DIM, 1);
	}
}

/** @brief ARM/DISARM, HOLD and CFG buttons. */
static void drawButtons() {
	// The pressed state is part of the key, or a button would light up under a
	// finger and stay lit until something else happened to invalidate the row.
	int pressKey = (pressing(BTN_ARM, s_touch) ? 1 : 0) |
	               (pressing(BTN_HOLD, s_touch) ? 2 : 0) |
	               (pressing(BTN_CFG, s_touch) ? 4 : 0);
	if (s_shown.holdOn == (int)s_hold && s_shown.btnArmed == (int)s_armed &&
	    s_shown.config == (int)s_config && s_shown.btnPress == pressKey)
		return;
	s_shown.btnPress = pressKey;
	s_shown.holdOn = s_hold;
	s_shown.btnArmed = s_armed;
	s_shown.config = s_config;

	// From Z_THR_Y1 + 1 rather than Z_BTN_Y0: the row between the two regions
	// belongs to neither, so nothing ever repaints it, and a glyph descender or
	// a bold smear landing there would persist for the rest of the session.
	gfxRect(0, Z_THR_Y1 + 1, GFX_W, Z_BTN_Y1 - Z_THR_Y1, C_BG);
	drawBtn(BTN_ARM, s_armed ? "DISARM" : "HOLD TO ARM",
	        s_armed ? C_RED : C_PANEL, s_armed ? C_ONACCENT : C_TEXT,
	        s_armed ? 2 : 1, pressing(BTN_ARM, s_touch));
	drawBtn(BTN_HOLD, "HOLD", s_hold ? C_BLUE : C_PANEL,
	        s_hold ? C_ONACCENT : C_DIM, 1, pressing(BTN_HOLD, s_touch));
	drawBtn(BTN_CFG, "CFG", C_PANEL, C_DIM, 1, pressing(BTN_CFG, s_touch));
}

/** @} */

/** @brief Full-screen settings overlay. */
static void drawConfig() {
	// Is EDT actually delivering, as opposed to having been asked for? The
	// enable is fire-and-forget -- the ESC never acknowledges it -- so arriving
	// frames are the only evidence, and the button cannot report success.
	EscReading r;
	escMerge(&s_tel, millis(), KISS_STALE_MS, EDT_STALE_MS, &r);
	int edtActive = (int)r.edtFresh;

	// The flash is part of the redraw key, so releasing it repaints too --
	// otherwise the button would light up and stay lit until something else
	// happened to invalidate the screen.
	int flashKey = cmdFlashActive() ? ((int)s_cmdFlash * 2 + (s_cmdFlashOk ? 1 : 0)) : 0;

	if (s_shown.config == 1 && s_shown.poles == s_poles &&
	    s_shown.maxPct == (int)((uint32_t)s_maxThrottle * 100 / 2000) &&
	    s_shown.cmdFlash == flashKey && s_shown.edtActive == edtActive &&
	    s_shown.dirty == (int)settingsDirty())
		return;
	s_shown.dirty = settingsDirty();
	s_shown.config = 1;
	s_shown.poles = s_poles;
	s_shown.maxPct = (int)((uint32_t)s_maxThrottle * 100 / 2000);
	s_shown.cmdFlash = flashKey;
	s_shown.edtActive = edtActive;

	gfxFill(C_BG);
	gfxRect(0, 0, GFX_W, 26, C_PANEL);
	gfxText(8, 9, "SETTINGS", C_TEXT, 2);

	// Poles and the throttle ceiling are persisted, but the button that persists
	// them is on SETUP. Saying so here is the difference between "my settings
	// reset themselves" and "I did not press save".
	if (settingsDirty()) gfxText(112, 12, "UNSAVED", C_AMBER, 1);

	// Read-only: EDT needs no button any more, since the firmware enables it
	// for each ESC as it appears. It is still worth showing, because "green"
	// and "all four telemetry tiles read --" are the same fact and one of them
	// is quicker to take in.
	const char *edtTxt = edtActive ? "EDT ON" : "EDT OFF";
	int chipW = gfxTextW(edtTxt, 1) + 12;
	gfxRoundRect(GFX_W - 6 - chipW, 5, chipW, 16, 4,
	             edtActive ? C_GREEN : C_RED);
	gfxText(GFX_W - 6 - chipW + 6, 9, edtTxt, C_ONACCENT, 1);


	char buf[24];

	gfxText(14, CFG_POLES_Y - 20, "MOTOR POLES", C_DIM, 1);
	drawBtn(BTN_POLES_M, "-", C_PANEL, C_TEXT, 2, pressing(BTN_POLES_M, s_touch));
	drawBtn(BTN_POLES_P, "+", C_PANEL, C_TEXT, 2, pressing(BTN_POLES_P, s_touch));
	snprintf(buf, sizeof(buf), "%d", s_poles);
	gfxText(120 - gfxTextW(buf, 3) / 2, 82, buf, C_TEXT, 3);

	gfxText(14, CFG_MAXT_Y - 20, "THROTTLE CEILING", C_DIM, 1);
	drawBtn(BTN_MAXT_M, "-", C_PANEL, C_TEXT, 2, pressing(BTN_MAXT_M, s_touch));
	drawBtn(BTN_MAXT_P, "+", C_PANEL, C_TEXT, 2, pressing(BTN_MAXT_P, s_touch));
	snprintf(buf, sizeof(buf), "%d%%", (int)((uint32_t)s_maxThrottle * 100 / 2000));
	gfxText(120 - gfxTextW(buf, 3) / 2, 158, buf, C_AMBER, 3);

	bool beepLit = cmdFlashActive();

	// White for an accepted press, amber for a refused one.
	// C_INK / C_PAPER rather than white / background: both of those flip with
	// the theme, so an inverted button stays inverted in either palette.
	drawBtn(BTN_BEEP, "BEEP", beepLit ? (s_cmdFlashOk ? C_INK : C_AMBER)
	                                  : C_PANEL,
	        beepLit ? C_PAPER : C_CYAN, 1, pressing(BTN_BEEP, s_touch));

	// The hint turns into the reason when a press is refused, so the amber
	// flash is explained rather than just noticed.
	bool refused = beepLit && !s_cmdFlashOk;
	gfxTextCenter(CFG_HINT_Y,
	              refused ? "REFUSED - DISARM THE ESC FIRST"
	                      : "BEEP NEEDS THE ESC DISARMED",
	              refused ? C_RED : C_DIM, 1);

	drawBtn(BTN_AM32, "AM32", C_PANEL, C_CYAN, 1, pressing(BTN_AM32, s_touch));
	drawBtn(BTN_LOG, "SD LOG", C_PANEL, C_CYAN, 1, pressing(BTN_LOG, s_touch));
	drawBtn(BTN_SETUP, "SETUP", C_PANEL, C_CYAN, 1, pressing(BTN_SETUP, s_touch));
	drawBtn(BTN_BACK, "BACK", C_PANEL, C_TEXT, 1, pressing(BTN_BACK, s_touch));
}

/** @brief One label/value row on the logging screen. */
static void drawLogRow(int row, const char *label, const char *value,
                       uint16_t vcol) {
	int y = LOG_ROW0_Y + row * LOG_ROW_H;
	gfxText(14, y, label, C_DIM, 1);
	gfxText(GFX_W - 14 - gfxTextW(value, 1), y, value, vcol, 1);
}

/**
 * @brief Logging status screen: card state, counters and manual start/stop.
 *
 * The counters are not decoration. `SD_LOG_BUFFER_BYTES` is a guess until it has
 * been measured, and BUF PEAK and WORST FLUSH are the two numbers to size it
 * from — a peak that approaches the buffer size, or a flush that takes longer
 * than the buffer holds, is the warning that it is too small. DROPS reaching
 * anything but zero says it already was.
 */
static void drawLogScreen() {
	SdLogStatus st;
	sdLogStatus(&st);

	if (s_shown.config == 2 &&
	    s_shown.logState == (int)st.state && s_shown.logFile == st.fileNumber &&
	    s_shown.logBytes == st.bytesWritten && s_shown.logFrames == st.framesLogged &&
	    s_shown.logDrops == (int)st.dropEvents && s_shown.logPeak == st.peakBuffer &&
	    s_shown.logWorstMs == st.worstFlushMs)
		return;

	s_shown.config     = 2;
	s_shown.logState   = (int)st.state;
	s_shown.logFile    = st.fileNumber;
	s_shown.logBytes   = st.bytesWritten;
	s_shown.logFrames  = st.framesLogged;
	s_shown.logDrops   = st.dropEvents;
	s_shown.logPeak    = st.peakBuffer;
	s_shown.logWorstMs = st.worstFlushMs;

	gfxFill(C_BG);
	gfxRect(0, 0, GFX_W, 26, C_PANEL);
	gfxText(8, 9, "SD LOG", C_TEXT, 2);

	char buf[32];
	const char *stateTxt;
	uint16_t stateCol;
	switch (st.state) {
		case SdLogState::Logging: stateTxt = "RECORDING"; stateCol = C_RED;   break;
		case SdLogState::Idle:    stateTxt = "READY";     stateCol = C_LIME;  break;
		case SdLogState::Error:   stateTxt = "CARD ERROR";stateCol = C_AMBER; break;
		default:                  stateTxt = "NO CARD";   stateCol = C_DIM;   break;
	}
	drawLogRow(0, "STATUS", stateTxt, stateCol);

	if (st.fileNumber) snprintf(buf, sizeof(buf), "LOG%05u.BFL", (unsigned)st.fileNumber);
	else               snprintf(buf, sizeof(buf), "--");
	drawLogRow(1, "FILE", buf, C_TEXT);

	snprintf(buf, sizeof(buf), "%lu", (unsigned long)st.framesLogged);
	drawLogRow(2, "FRAMES", buf, C_TEXT);

	// kB rather than bytes: at ~7 kB/s the byte count is unreadable within
	// seconds, and the useful question is "is it growing".
	snprintf(buf, sizeof(buf), "%lu kB", (unsigned long)(st.bytesWritten / 1024u));
	drawLogRow(3, "WRITTEN", buf, C_TEXT);

	snprintf(buf, sizeof(buf), "%lu", (unsigned long)st.dropEvents);
	drawLogRow(4, "DROPPED FRAMES", buf, st.dropEvents ? C_RED : C_DIM);

	// Against the configured size, so "how close to the edge" needs no mental
	// arithmetic.
	snprintf(buf, sizeof(buf), "%lu / %u B", (unsigned long)st.peakBuffer,
	         (unsigned)SD_LOG_BUFFER_BYTES);
	uint16_t peakCol = C_TEXT;
	if (st.peakBuffer > SD_LOG_BUFFER_BYTES / 2) peakCol = C_AMBER;
	if (st.peakBuffer > (SD_LOG_BUFFER_BYTES * 3) / 4) peakCol = C_RED;
	drawLogRow(5, "BUF PEAK", buf, peakCol);

	snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)st.worstFlushMs);
	drawLogRow(6, "WORST FLUSH", buf,
	           st.worstFlushMs > 50 ? C_AMBER : C_TEXT);

	// What the card itself reported. Non-zero here with a failed mount means the
	// card is on the bus and talking -- the fault is the filesystem, not the
	// wiring -- and that is the single most useful thing to know when a card
	// "is not detected".
	// Size, not type: card_type is an SPI-mode concept and the SDIO driver
	// leaves it at zero, so keying presence off it would report "NONE" for a
	// card that has just mounted.
	static const char *TYPE_TXT[5] = {"", "SDSC v1 ", "SDSC v2 ", "SDHC/XC ", ""};
	uint8_t ct = st.cardType < 4 ? st.cardType : 4;
	if (st.cardSizeMB >= 1024)
		snprintf(buf, sizeof(buf), "%s%lu.%luGB", TYPE_TXT[ct],
		         (unsigned long)(st.cardSizeMB / 1024),
		         (unsigned long)((st.cardSizeMB % 1024) * 10 / 1024));
	else if (st.cardSizeMB)
		snprintf(buf, sizeof(buf), "%s%luMB", TYPE_TXT[ct],
		         (unsigned long)st.cardSizeMB);
	else
		snprintf(buf, sizeof(buf), "NONE");
	drawLogRow(7, "CARD", buf, st.cardSizeMB ? C_TEXT : C_DIM);

	// FatFs FRESULT. 0 is FR_OK, 3 FR_NOT_READY (nothing answered), 13
	// FR_NO_FILESYSTEM (card fine, no filesystem FatFs can read).
	static const char *FR_TXT[] = {
		"OK", "DISK ERR", "INT ERR", "NOT READY", "NO FILE", "NO PATH",
		"BAD NAME", "DENIED", "EXIST", "BAD OBJ", "WRITE PROT",
		"BAD DRIVE", "NOT ENABLED", "NO FILESYSTEM",
	};
	if (st.mountResult < sizeof(FR_TXT) / sizeof(FR_TXT[0]))
		snprintf(buf, sizeof(buf), "%u %s", st.mountResult, FR_TXT[st.mountResult]);
	else
		snprintf(buf, sizeof(buf), "%u", st.mountResult);
	drawLogRow(8, "MOUNT", buf, st.mountResult ? C_AMBER : C_LIME);

	bool active = (st.state == SdLogState::Logging);
	bool usable = (st.state != SdLogState::NoCard);
	drawBtn(BTN_LOG_TOGGLE, active ? "STOP" : "START",
	        usable ? (active ? C_RED : C_PANEL) : C_PANEL,
	        usable ? (active ? C_ONACCENT : C_LIME) : C_GRID, 2,
	        pressing(BTN_LOG_TOGGLE, s_touch));

	// The card is only mounted once, at boot, so one inserted afterwards needs
	// this. Without it, "insert card, nothing happens" is indistinguishable
	// from a card the firmware cannot read.
	drawBtn(BTN_LOG_RETRY, "RETRY MOUNT", C_PANEL, C_CYAN, 1,
	        pressing(BTN_LOG_RETRY, s_touch));
	drawBtn(BTN_LOG_BACK, "BACK", C_PANEL, C_TEXT, 1,
	        pressing(BTN_LOG_BACK, s_touch));
}

/** @brief Touch handling for the logging screen. All three fire on release. */
static void handleLogTouch() {
	if (s_touch.down) s_shown.config = -1;   // keep the pressed look live

	if (tapped(BTN_LOG_TOGGLE, s_touch)) {
		if (sdLogActive()) sdLogStop();
		else               sdLogStart();
		s_shown.config = -1;
	} else if (tapped(BTN_LOG_RETRY, s_touch)) {
		sdLogRemount();
		s_shown.config = -1;
	} else if (tapped(BTN_LOG_BACK, s_touch)) {
		s_logScreen = false;
		invalidateAll();
		gfxFill(C_BG);
	}
}

/**
 * @brief Dispatch input on the settings overlay.
 *
 * Two idioms, and which one a control gets follows from what it does. A stepper
 * repeats while held, because its range is wider than anyone wants to tap
 * across -- the throttle ceiling is twenty steps end to end. Everything else
 * fires on release, so it can be cancelled by sliding off.
 */
static void handleConfigTouch() {
	int x = s_touch.x, y = s_touch.y;

	// --- steppers: repeat while held ---
	int polesDir = pressing(BTN_POLES_M, s_touch) ? -1
	             : pressing(BTN_POLES_P, s_touch) ? +1 : 0;
	if (repeatFires(&s_polesRepeat, polesDir, millis())) {
		int p = (int)s_poles + 2 * polesDir;
		if (p >= MIN_MOTOR_POLES && p <= MAX_MOTOR_POLES) {
			s_poles = (uint8_t)p;
			escSetPoles(s_poles);
			s_shown.poles = -1;
		}
	}

	int maxtDir = pressing(BTN_MAXT_M, s_touch) ? -1
	            : pressing(BTN_MAXT_P, s_touch) ? +1 : 0;
	if (repeatFires(&s_maxtRepeat, maxtDir, millis())) {
		int t = (int)s_maxThrottle + MAX_THROTTLE_STEP * maxtDir;
		if (t >= MAX_THROTTLE_STEP && t <= MAX_THROTTLE_CEILING) {
			s_maxThrottle = (uint16_t)t;
			s_shown.maxPct = -1;
		}
	}
	if (polesDir || maxtDir) return;

	// --- everything else: fires on release, inside, having started inside ---
	if (s_touch.down) { s_shown.cmdFlash = -1; }   // keep the pressed look live
	if (tapped(BTN_BEEP, s_touch)) {
		cmdFlashSet(CmdFlash::Beep, escRequestBeep(1));
	} else if (tapped(BTN_AM32, s_touch)) {
		s_am32 = true;
		gfxFill(C_BG);
		uiAm32Enter();
	} else if (tapped(BTN_LOG, s_touch)) {
		s_logScreen = true;
		s_config = false;
		s_shown.config = -1;
		gfxFill(C_BG);
	} else if (tapped(BTN_SETUP, s_touch)) {
		s_setup = true;
		gfxFill(C_BG);
		uiSetupEnter();
	} else if (tapped(BTN_BACK, s_touch)) {
		s_config = false;
		invalidateAll();
		gfxFill(C_BG);
	}
	(void)x; (void)y;
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
	// Unconditional backstop. Also unreachable today: every drag surface above
	// is gated on s_armed, and every path that clears it zeroes the throttle on
	// the way. It exists so that adding a surface which forgets to check cannot
	// leave a throttle standing.
	if (!s_armed) s_throttle = 0;

	// --- arm button: press and hold ---
	if (s_touch.down && hit(BTN_ARM, x, y) && hit(BTN_ARM, s_touch.downX, s_touch.downY)) {
		if (s_armed) {
			// Disarm is instant, on press. The one control that deliberately
			// does not wait for release: everything else here can afford the
			// cancel gesture, and the button whose job is "stop the motor now"
			// cannot.
			if (s_touch.pressed) {
				s_armed = false;
				s_throttle = 0;
				s_hold = false;
				escSetArmed(false);
				backlightDip();
				invalidateAll();
			}
		} else {
			if (!s_armPressing) {
				s_armPressing = true;
				s_armPressStart = millis();
			}
			// Belt and braces, and currently unreachable: arming already
			// requires ARM_HOLD_MS of holding, throughout which the throttle is
			// necessarily zero, and ARM_HOLD_MS (1000) is longer than
			// ARM_ZERO_THROTTLE_MS (250). It binds only if the hold is ever
			// shortened below the window. Kept deliberately -- a mutation test
			// confirms no test can reach it, which is a property of the
			// constants rather than a gap in coverage.
			bool zeroLongEnough =
			    (uint32_t)(millis() - s_zeroSince) >= ARM_ZERO_THROTTLE_MS;
			if (zeroLongEnough &&
			    (uint32_t)(millis() - s_armPressStart) >= ARM_HOLD_MS) {
				s_armed = true;
				s_throttle = 0;
				escSetArmed(true);
				s_armPressing = false;
				backlightDip();
				invalidateAll();
			}
		}
	} else {
		s_armPressing = false;
	}

	if (tapped(BTN_HOLD, s_touch)) {
		s_hold = !s_hold;
		if (!s_hold) s_throttle = 0;
	} else if (tapped(BTN_CFG, s_touch)) {
		bool wasArmed = s_armed;
		s_config = true;
		s_armed = false;
		s_throttle = 0;
		s_hold = false;
		escSetArmed(false);
		if (wasArmed) backlightDip();
		invalidateAll();
		s_shown.config = -1;
	}
}

void uiDrawSplash() {
	gfxFill(C_BG);
	gfxTextCenter(104, "DSHOT", C_LIME, 4);
	gfxTextCenter(144, "DISPLAY", C_TEXT, 4);
	gfxTextCenter(188, "BIDIRECTIONAL ESC TESTER", C_DIM, 1);
	gfxTextCenter(216, "JuWi made", C_CYAN, 2);
	// Which board this image was built for. Cheap here, and the alternative is
	// working it out by flashing a UF2 and seeing whether the screen lights up.
	gfxTextCenter(248, g_board->label, C_GRID, 1);
}

uint16_t uiThrottle() { return s_throttle; }
bool uiArmed() { return s_armed; }

void uiInit() {
	memset(&s_touch, 0, sizeof(s_touch));
	memset(&s_tel, 0, sizeof(s_tel));

	// Which screen is showing is UI state too, and this function claims to reset
	// UI state. It did not, which on hardware is invisible -- it runs once, at
	// boot, from a cold start -- and in the test suite meant a case that left
	// the settings overlay up handed it to whatever ran next, where taps landed
	// on a screen the test did not think it was looking at.
	s_config = false;
	s_logScreen = false;
	s_am32 = false;
	s_setup = false;
	s_armed = false;
	s_hold = false;
	s_throttle = 0;
	s_armPressing = false;
	s_dragging = false;
	s_padDragging = false;
	s_padEngaged = false;
	s_cmdFlash = CmdFlash::None;
	s_blDipUntil = 0;
	s_polesRepeat = Repeat{};
	s_maxtRepeat = Repeat{};

	invalidateAll();

	// 12-bit SAR, 3.3 V reference. adc_init() must precede the GPIO setup.
	adc_init();
	adc_gpio_init(g_board->batAdcPin);

	// The palette and backlight follow the stored preference before the first
	// frame, so a board saved in high contrast never flashes a dark screen on
	// the way up.
	themeSet(settings()->highContrast ? Theme::HighContrast : Theme::Dark);
	st7789SetBacklight(themeBacklight(settings()->backlight));

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
	adc_select_input(g_board->batAdcChan);
	int raw = (int)adc_read();
	float v = (raw * 3.3f / 4095.0f) * g_board->batDivider;
	s_batteryV = s_batteryV == 0.0f ? v : (s_batteryV * 0.9f + v * 0.1f);

	if (s_touch.down) s_lastTouchMs = millis();

	// Backlight, every frame: the level is a function of the theme, the stored
	// preference and whether a dip is running, so recomputing it is simpler than
	// tracking who last changed which of the three.
	{
		uint8_t want = themeBacklight(settings()->backlight);
		if ((int32_t)(millis() - s_blDipUntil) < 0) want = (uint8_t)(want / BL_DIP_DIV);
		st7789SetBacklight(want);
	}

	// SETUP owns the whole screen while it runs. It can change the pin the pump
	// drives and the palette everything is rendered in, so leaving it has to
	// invalidate every cached region -- a theme swap is invisible to caches that
	// only remember values.
	if (s_setup) {
		if (!uiSetupTick(&s_touch)) {
			s_setup = false;
			invalidateAll();
			gfxFill(C_BG);
			s_shown.config = -1;
		}
		st7789FlushDirty();
		return;
	}

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

	// Told here rather than from loop(): the UI owns the arm state, and putting
	// the coupling in main.cpp put it in the one file the host tests never run,
	// so a test covering it could pass against a broken rule.
	sdLogSetArmed(s_armed);

	// Each handler can navigate away, so each draw is conditional on still being
	// on that screen. Painting it anyway is not merely wasted work: the press
	// that leaves also calls gfxFill(), so the extra paint puts the old screen
	// back over a cleared frame, and whatever it leaves on a row the incoming
	// screen's regions do not cover stays there. The residue was invisible for
	// as long as the palette was dark on black; it is not in high contrast.
	if (s_logScreen) {
		handleLogTouch();
		if (s_logScreen) drawLogScreen();
	} else if (s_config) {
		handleConfigTouch();
		if (s_config && !s_am32 && !s_setup) drawConfig();
	} else {
		handleMainTouch();

		// idle auto-disarm
#if IDLE_DISARM_MS > 0
		if (s_armed && (uint32_t)(millis() - s_lastTouchMs) > IDLE_DISARM_MS) {
			s_armed = false;
			s_throttle = 0;
			s_hold = false;
			escSetArmed(false);
			backlightDip();
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
