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
#include "ui_widgets.h"
#include "settings.h"
#include "gfx.h"
#include "touch.h"
#include "st7789.h"
#include "esc_task.h"
#include "esc_merge.h"
#include "rpm_filter.h"
#include "sd_log.h"
#include "usb_msc.h"
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
// The regions tile the panel exactly: each ends where the next begins. Gaps
// here are rows no region ever repaints, so whatever the previous screen left
// in them survives. The screen-change clears already keep those rows blank in
// practice; the asserts make the property structural rather than coincidental,
// so the next layout edit cannot quietly reopen a one-row leak.
#define Z_STATUS_Y0   0
#define Z_STATUS_Y1   26
#define Z_RPM_Y0      27
#define Z_RPM_Y1      127
#define Z_TELE_Y0     128
#define Z_TELE_Y1     233
#define Z_THR_Y0      234
#define Z_THR_Y1      278
#define Z_BTN_Y0      279
#define Z_BTN_Y1      319

static_assert(Z_STATUS_Y0 == 0, "regions must start at the top of the panel");
static_assert(Z_RPM_Y0  == Z_STATUS_Y1 + 1, "gap between status and RPM");
static_assert(Z_TELE_Y0 == Z_RPM_Y1 + 1,    "gap between RPM and telemetry");
static_assert(Z_THR_Y0  == Z_TELE_Y1 + 1,   "gap between telemetry and throttle");
static_assert(Z_BTN_Y0  == Z_THR_Y1 + 1,    "gap between throttle and buttons");
static_assert(Z_BTN_Y1  == GFX_H - 1,       "regions must reach the bottom");

#define THR_TRACK_X   8
#define THR_TRACK_Y   248
#define THR_TRACK_W   224
#define THR_TRACK_H   26
#define THR_TOUCH_PAD 8       // vertical grab slop around the track

/**
 * @brief Track columns at each end that count as the rail rather than as a
 *        position on it.
 *
 * The last few columns of the panel are not reachable with a finger: the case
 * rim overhangs the glass, and the fingertip stops short of the edge even when
 * the touch controller would happily report it. A slider whose ends exist only
 * in theory cannot be set to zero or to the ceiling, which is what a 2" board
 * reported as a 20 % ceiling that would only reach 19 %.
 *
 * So the ends saturate: @ref THR_USABLE_W is what maps onto 0..ceiling, and
 * everything outside it is the rail it is nearest. The drawn track keeps its
 * full width, so the bar still fills to its visible end -- it just gets there
 * while the finger is still clear of the rim.
 *
 * 16 px is a little over 7 % of the track, and comfortably more than the ~11 px
 * that report was short by.
 */
#define THR_EDGE_SAT  16
#define THR_USABLE_W  (THR_TRACK_W - 2 * THR_EDGE_SAT)
static_assert(THR_USABLE_W > 0, "the saturation margins have eaten the track");
static_assert(THR_EDGE_SAT * 2 < THR_TRACK_W / 2,
              "saturating half the track would make it a two-position switch");

// The number-display area doubles as a large relative throttle pad. It runs
// from the top of the RPM readout down to where the gauge's grab area starts,
// so the two regions abut exactly: no overlap, no dead strip between them.
#define PAD_Y0        Z_RPM_Y0
#define PAD_Y1        (THR_TRACK_Y - THR_TOUCH_PAD - 1)
#define PAD_H         (PAD_Y1 - PAD_Y0 + 1)

/** @} */

// The main screen is the root, so it has no header and no BACK: there is
// nowhere to go back to, and a disabled or missing control in the place the
// habit says to look is worse than the habit not applying here. CFG is its
// navigation, and it sits with the other two actions.
static const UiRect BTN_ARM  = { 6, 283, 104, 33 };
static const UiRect BTN_HOLD = { 116, 283, 54, 33 };
static const UiRect BTN_CFG  = { 176, 283, 58, 33 };

// config screen
/**
 * @defgroup ui_cfg_layout Settings screen layout
 * @brief Row positions, with the gaps asserted rather than eyeballed.
 *
 * Everything below the header is derived from @ref UI_BODY_Y and from the row
 * above it, rather than being a hand-picked pixel row. That is not tidiness:
 * the previous version was eleven independent literals, so moving one row meant
 * finding every other number that had quietly been chosen to clear it, and the
 * asserts at the bottom of this block are all scar tissue from a case where
 * that went wrong.
 *
 * The screen also got shorter by one control. BACK used to be an 18 px strip
 * across the bottom — below @ref UI_TAP_MIN, and in a different place from the
 * BACK on SETUP and AM32. It is in the header now, and every row here spent the
 * space it freed on being big enough to hit.
 * @{
 */
#define CFG_ROW_H      44   /**< Height of a control row. Was 38-40. */
#define CFG_POLES_LBL  UI_BODY_Y                        /**< "MOTOR POLES". */
#define CFG_POLES_Y    (CFG_POLES_LBL + 12)             /**< Pole-count -/+ row. */
#define CFG_MAXT_LBL   (CFG_POLES_Y + CFG_ROW_H + 10)   /**< "THROTTLE CEILING". */
#define CFG_MAXT_Y     (CFG_MAXT_LBL + 12)              /**< Ceiling -/+ row. */
#define CFG_CMD_Y      (CFG_MAXT_Y + CFG_ROW_H + 14)    /**< BEEP row. */
#define CFG_HINT_Y     (CFG_CMD_Y + CFG_ROW_H + 6)      /**< Caption under BEEP. */
#define CFG_NAV_Y      (CFG_HINT_Y + 16)                /**< AM32 / SD LOG / SETUP. */
#define CFG_NAV_H      48
#define CFG_STEP_W     46   /**< Width of a `-` or `+` target. */
/** @} */

static const UiRect BTN_POLES_M = { 14, CFG_POLES_Y, CFG_STEP_W, CFG_ROW_H };
static const UiRect BTN_POLES_P = { 180, CFG_POLES_Y, CFG_STEP_W, CFG_ROW_H };
static const UiRect BTN_MAXT_M  = { 14, CFG_MAXT_Y, CFG_STEP_W, CFG_ROW_H };
static const UiRect BTN_MAXT_P  = { 180, CFG_MAXT_Y, CFG_STEP_W, CFG_ROW_H };
// BEEP has the row to itself. The EDT enable used to sit beside it and is
// gone: the firmware now sends it whenever an ESC starts answering, so the
// button was a control for something already handled. What is left of EDT here
// is the read-only chip on the strip under the title.
static const UiRect BTN_BEEP    = { 14, CFG_CMD_Y, 212, CFG_ROW_H };
// One row, three destinations -- 3 x 68 plus two 4 px gaps is exactly the
// 212 px between the margins, and the assert below keeps it that way.
#define CFG_NAV_W    68
#define CFG_NAV_GAP  4
static const UiRect BTN_AM32  = { 14, CFG_NAV_Y, CFG_NAV_W, CFG_NAV_H };
static const UiRect BTN_LOG   = { 14 + CFG_NAV_W + CFG_NAV_GAP, CFG_NAV_Y,
                                  CFG_NAV_W, CFG_NAV_H };
static const UiRect BTN_SETUP = { 14 + 2 * (CFG_NAV_W + CFG_NAV_GAP), CFG_NAV_Y,
                                  CFG_NAV_W, CFG_NAV_H };

static_assert(14 + 3 * CFG_NAV_W + 2 * CFG_NAV_GAP <= 226,
              "the AM32/LOG/SETUP row is too wide");

/**
 * @defgroup ui_log_layout Logging screen layout
 * @{
 */
#define LOG_ROW0_Y     (UI_HDR_H + 8)   /**< First status row. */
#define LOG_ROW_H      21
#define LOG_ROWS        9   /**< Through MOUNT; CARD and MOUNT are diagnostics. */
#define LOG_NOTE_Y    228   /**< One line: why a button just refused. */
#define LOG_TOGGLE_Y  240   /**< START / STOP, or EJECT while a host has the card. */
#define LOG_TOGGLE_H   44
#define LOG_BOT_Y     288   /**< RETRY MOUNT and USB DRIVE, side by side. */
#define LOG_BOT_H      30
/** @} */

static const UiRect BTN_LOG_TOGGLE = { 14, LOG_TOGGLE_Y, 212, LOG_TOGGLE_H };
// The bottom row splits rather than the screen growing: 2 x 104 with a 4 px gap
// is the 212 px between the margins, the same arithmetic the settings nav row
// uses. Both halves clear UI_TAP_MIN, which is the point of splitting rather
// than stacking.
#define LOG_BTN_W    104
#define LOG_BTN_GAP    4
static const UiRect BTN_LOG_RETRY  = { 14, LOG_BOT_Y, LOG_BTN_W, LOG_BOT_H };
static const UiRect BTN_LOG_USB    = { 14 + LOG_BTN_W + LOG_BTN_GAP, LOG_BOT_Y,
                                       LOG_BTN_W, LOG_BOT_H };

static_assert(LOG_ROW0_Y >= UI_HDR_H, "logging rows overlap the header");
static_assert(LOG_ROW0_Y + LOG_ROWS * LOG_ROW_H <= LOG_NOTE_Y,
              "logging rows overlap the note line");
static_assert(LOG_NOTE_Y + 7 <= LOG_TOGGLE_Y,
              "the note line overlaps the START/STOP button");
static_assert(LOG_TOGGLE_Y + LOG_TOGGLE_H <= LOG_BOT_Y,
              "START/STOP button overlaps the bottom row");
static_assert(LOG_BOT_Y + LOG_BOT_H <= GFX_H,
              "the bottom row runs off the panel");
static_assert(LOG_TOGGLE_H >= UI_TAP_MIN && LOG_BOT_H >= UI_TAP_MIN,
              "a logging button is smaller than a fingertip");
// Against the constants rather than the structs: a UiRect is const, not
// constexpr, so its members are not usable in a constant expression. The nav
// row on the settings screen is asserted the same way, for the same reason.
static_assert(14 + 2 * LOG_BTN_W + LOG_BTN_GAP <= 226,
              "RETRY and USB DRIVE do not fit between the margins");

// Caught by a screenshot rather than by reading the code: the caption used to
// sit at y=240, inside the band the command row occupies, and was drawn
// straight through it. Assert the gaps so it cannot recur.
static_assert(CFG_POLES_LBL >= UI_BODY_Y,
              "the first settings caption runs into the strip");
static_assert(CFG_MAXT_LBL >= CFG_POLES_Y + CFG_ROW_H,
              "the ceiling caption overlaps the pole stepper");
static_assert(CFG_CMD_Y >= CFG_MAXT_Y + CFG_ROW_H,
              "BEEP overlaps the ceiling stepper");
static_assert(CFG_HINT_Y >= CFG_CMD_Y + CFG_ROW_H,
              "settings caption overlaps the BEEP button");
static_assert(CFG_NAV_Y >= CFG_HINT_Y + 7,
              "the AM32/LOG/SETUP row overlaps the caption");
static_assert(CFG_NAV_Y + CFG_NAV_H <= GFX_H,
              "the AM32/LOG/SETUP row runs off the panel");
static_assert(CFG_ROW_H >= UI_TAP_MIN && CFG_NAV_H >= UI_TAP_MIN,
              "a settings button is smaller than a fingertip");

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

/**
 * @brief Smoothing for the two speed readouts. @see rpm_filter.h
 *
 * One filter, on eRPM, with RPM derived from its output rather than filtered
 * separately. Two filters would drift apart under acceleration and put a pole
 * count's worth of disagreement between two numbers sitting one above the
 * other, which is a thing people notice and reasonably report as a bug.
 */
static RpmFilter s_erpmFilter;
/** @brief Filtered eRPM, stepped once per uiTick() and read by drawRpm(). */
static uint32_t  s_erpmShown = 0;
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
	int  mscState, mscNote;
	uint32_t mscBlocks;
} s_shown;

/** @brief Force every region to repaint on the next uiTick(). */
static void invalidateAll() { memset(&s_shown, 0xFF, sizeof(s_shown)); }

// drawBtn(), hit(), pressing() and tapped() used to live here, and near-copies
// of all four lived in ui_setup.cpp and ui_am32.cpp. They are uiButton(),
// uiPressing() and uiTapped() now. @see ui_widgets.h
//
// These two adapt the shared helpers to this file's habit of passing the
// TouchState by reference, which is worth keeping: every call site here already
// has s_touch in scope and none of them can pass null.
/** @brief True while @p b is touched by a press that began inside it. */
static bool pressing(const UiRect &b, const TouchState &t) {
	return uiPressing(b, &t);
}

/** @brief True on the frame a tap of @p b completes. */
static bool tapped(const UiRect &b, const TouchState &t) {
	return uiTapped(b, &t);
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
	// Both from the one filtered figure, so the big number and the eRPM line
	// under it can never disagree. The pole conversion is the same one core1
	// does; doing it here rather than reading s_tel.rpm is what keeps them tied.
	uint32_t erpm = alive ? s_erpmShown : 0;
	uint32_t rpm  = (alive && s_poles >= 2) ? erpm / (s_poles / 2) : 0;
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

	// The regions tile exactly (asserted at their definitions), so this is the
	// whole button band and nothing but it. It used to start a row early to
	// cover a boundary row that belonged to neither region.
	gfxRect(0, Z_BTN_Y0, GFX_W, Z_BTN_Y1 - Z_BTN_Y0 + 1, C_BG);
	uiButton(BTN_ARM, s_armed ? "DISARM" : "HOLD TO ARM",
	        s_armed ? C_RED : C_PANEL, s_armed ? C_ONACCENT : C_TEXT,
	        s_armed ? 2 : 1, pressing(BTN_ARM, s_touch));
	uiButton(BTN_HOLD, "HOLD", s_hold ? C_BLUE : C_PANEL,
	        s_hold ? C_ONACCENT : C_DIM, 1, pressing(BTN_HOLD, s_touch));
	uiButton(BTN_CFG, "CFG", C_PANEL, C_DIM, 1, pressing(BTN_CFG, s_touch));
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
	uiHeader("SETTINGS", &s_touch);

	uiStripClear();

	// Poles and the throttle ceiling are persisted, but the button that persists
	// them is on SETUP. Saying so here is the difference between "my settings
	// reset themselves" and "I did not press save".
	if (settingsDirty()) uiStripText("UNSAVED", C_AMBER);

	// Read-only: EDT needs no button any more, since the firmware enables it
	// for each ESC as it appears. It is still worth showing, because "green"
	// and "all four telemetry tiles read --" are the same fact and one of them
	// is quicker to take in. On the strip rather than in the header, because the
	// header's right-hand end is BACK's on every screen now.
	uiChip(edtActive ? "EDT ON" : "EDT OFF", edtActive ? C_GREEN : C_RED);

	char buf[24];

	// Value text is centred in its row rather than placed at a literal y, so
	// changing CFG_ROW_H moves the number with the buttons. It did not, and the
	// number sat a few pixels high for two releases.
	gfxText(14, CFG_POLES_LBL, "MOTOR POLES", C_DIM, 1);
	uiButton(BTN_POLES_M, "-", C_PANEL, C_TEXT, 2, pressing(BTN_POLES_M, s_touch));
	uiButton(BTN_POLES_P, "+", C_PANEL, C_TEXT, 2, pressing(BTN_POLES_P, s_touch));
	snprintf(buf, sizeof(buf), "%d", s_poles);
	gfxText(120 - gfxTextW(buf, 3) / 2, CFG_POLES_Y + (CFG_ROW_H - 21) / 2,
	        buf, C_TEXT, 3);

	gfxText(14, CFG_MAXT_LBL, "THROTTLE CEILING", C_DIM, 1);
	uiButton(BTN_MAXT_M, "-", C_PANEL, C_TEXT, 2, pressing(BTN_MAXT_M, s_touch));
	uiButton(BTN_MAXT_P, "+", C_PANEL, C_TEXT, 2, pressing(BTN_MAXT_P, s_touch));
	snprintf(buf, sizeof(buf), "%d%%", (int)((uint32_t)s_maxThrottle * 100 / 2000));
	gfxText(120 - gfxTextW(buf, 3) / 2, CFG_MAXT_Y + (CFG_ROW_H - 21) / 2,
	        buf, C_AMBER, 3);

	bool beepLit = cmdFlashActive();

	// White for an accepted press, amber for a refused one.
	// C_INK / C_PAPER rather than white / background: both of those flip with
	// the theme, so an inverted button stays inverted in either palette.
	uiButton(BTN_BEEP, "BEEP", beepLit ? (s_cmdFlashOk ? C_INK : C_AMBER)
	                                  : C_PANEL,
	        beepLit ? C_PAPER : C_CYAN, 1, pressing(BTN_BEEP, s_touch));

	// The hint turns into the reason when a press is refused, so the amber
	// flash is explained rather than just noticed.
	bool refused = beepLit && !s_cmdFlashOk;
	gfxTextCenter(CFG_HINT_Y,
	              refused ? "REFUSED - DISARM THE ESC FIRST"
	                      : "BEEP NEEDS THE ESC DISARMED",
	              refused ? C_RED : C_DIM, 1);

	uiButton(BTN_AM32, "AM32", C_PANEL, C_CYAN, 1, pressing(BTN_AM32, s_touch));
	uiButton(BTN_LOG, "SD LOG", C_PANEL, C_CYAN, 1, pressing(BTN_LOG, s_touch));
	uiButton(BTN_SETUP, "SETUP", C_PANEL, C_CYAN, 1, pressing(BTN_SETUP, s_touch));
	// No BACK here: uiHeader() drew it, in the same place as on every other
	// screen. The 18 px strip that used to be along the bottom is what this
	// whole rework started from.
}

/** @brief One label/value row on the logging screen. */
static void drawLogRow(int row, const char *label, const char *value,
                       uint16_t vcol) {
	uiRow(LOG_ROW0_Y + row * LOG_ROW_H, LOG_ROW_H, label, value, vcol, 1);
}

/**
 * @brief What the last USB DRIVE press was refused for, if it was.
 *
 * Latched rather than recomputed, because the reason stops being true the
 * moment it is acted on -- disarm and the "DISARM FIRST" that told you to would
 * vanish before you had read it.
 */
static MscRefusal s_mscNote = MscRefusal::None;
static uint32_t   s_mscNoteUntilMs = 0;

/** @brief True while a refusal is still worth showing. */
static bool mscNoteActive() {
	return s_mscNote != MscRefusal::None &&
	       (int32_t)(millis() - s_mscNoteUntilMs) < 0;
}

/**
 * @brief The body of the logging screen while a host owns the card.
 *
 * Replaces the counters rather than sitting beside them, because none of them
 * mean anything now: the logger has let go of the card and its numbers are
 * frozen at whatever they were. A screen showing live-looking counters that
 * cannot move is the same lie the telemetry tiles refuse to tell.
 */
static void drawUsbPanel(MscState st) {
	gfxRect(0, LOG_ROW0_Y, GFX_W, LOG_NOTE_Y - LOG_ROW0_Y, C_BG);

	bool serving = (st == MscState::Serving);
	gfxTextCenter(LOG_ROW0_Y + 24, mscStateText(st), serving ? C_LIME : C_AMBER, 3);

	if (serving) {
		gfxTextCenter(LOG_ROW0_Y + 62, "THE CARD IS ON YOUR COMPUTER", C_TEXT, 1);
		gfxTextCenter(LOG_ROW0_Y + 78, "READ-ONLY - NOTHING CAN BE WRITTEN", C_DIM, 1);

		// Progress, because full speed USB moves about a megabyte a second and
		// a big card takes minutes. Without a number that moves, a long copy
		// and a hung one look identical.
		char buf[32];
		uint32_t kb = mscBlocksRead() / 2u;
		if (kb >= 1024) snprintf(buf, sizeof(buf), "%lu.%lu MB READ",
		                         (unsigned long)(kb / 1024), (unsigned long)((kb % 1024) * 10 / 1024));
		else            snprintf(buf, sizeof(buf), "%lu kB READ", (unsigned long)kb);
		gfxTextCenter(LOG_ROW0_Y + 104, buf, C_CYAN, 2);

		gfxTextCenter(LOG_ROW0_Y + 138, "EJECT ON THE COMPUTER FIRST,", C_GRID, 1);
		gfxTextCenter(LOG_ROW0_Y + 152, "OR PRESS EJECT BELOW", C_GRID, 1);
	} else {
		// Handover and Reclaim are both brief. Saying which one it is beats a
		// spinner: they fail differently, and a screen stuck on one of them is
		// worth being able to name in a bug report.
		gfxTextCenter(LOG_ROW0_Y + 70, "ONE MOMENT", C_DIM, 1);
	}
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

	MscState msc = mscGetState();
	int note = mscNoteActive() ? (int)s_mscNote + 1 : 0;
	// Blocks read is in the key so the progress figure actually advances, and
	// only while serving -- otherwise a stale counter would repaint the whole
	// screen forever at whatever the last read left it on.
	uint32_t blocks = (msc == MscState::Serving) ? mscBlocksRead() : 0;

	if (s_shown.config == 2 &&
	    s_shown.logState == (int)st.state && s_shown.logFile == st.fileNumber &&
	    s_shown.logBytes == st.bytesWritten && s_shown.logFrames == st.framesLogged &&
	    s_shown.logDrops == (int)st.dropEvents && s_shown.logPeak == st.peakBuffer &&
	    s_shown.logWorstMs == st.worstFlushMs &&
	    s_shown.mscState == (int)msc && s_shown.mscNote == note &&
	    s_shown.mscBlocks == blocks)
		return;

	s_shown.mscState   = (int)msc;
	s_shown.mscNote    = note;
	s_shown.mscBlocks  = blocks;
	s_shown.config     = 2;
	s_shown.logState   = (int)st.state;
	s_shown.logFile    = st.fileNumber;
	s_shown.logBytes   = st.bytesWritten;
	s_shown.logFrames  = st.framesLogged;
	s_shown.logDrops   = st.dropEvents;
	s_shown.logPeak    = st.peakBuffer;
	s_shown.logWorstMs = st.worstFlushMs;

	gfxFill(C_BG);
	uiHeader("SD LOG", &s_touch);

	if (msc != MscState::Idle) {
		drawUsbPanel(msc);
		// One button, and it is the way out. START, STOP and RETRY MOUNT all
		// need the card, and the card is not ours; drawing them disabled would
		// be four controls saying no where one control saying "give it back"
		// is the only thing anybody wants.
		uiButton(BTN_LOG_TOGGLE, "EJECT", C_AMBER, C_PAPER, 2,
		         pressing(BTN_LOG_TOGGLE, s_touch));
		gfxRect(0, LOG_BOT_Y, GFX_W, LOG_BOT_H, C_BG);
		return;
	}

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
	uiButton(BTN_LOG_TOGGLE, active ? "STOP" : "START",
	        usable ? (active ? C_RED : C_PANEL) : C_PANEL,
	        usable ? (active ? C_ONACCENT : C_LIME) : C_GRID, 2,
	        pressing(BTN_LOG_TOGGLE, s_touch));

	// The card is only mounted once, at boot, so one inserted afterwards needs
	// this. Without it, "insert card, nothing happens" is indistinguishable
	// from a card the firmware cannot read.
	uiButton(BTN_LOG_RETRY, "RETRY", C_PANEL, C_CYAN, 1,
	        pressing(BTN_LOG_RETRY, s_touch));

	// Hands the card to a computer over the same USB cable that powers the
	// board. Cyan like the other navigation-ish controls; it is not
	// destructive, and it is refused rather than guarded by a hold.
	uiButton(BTN_LOG_USB, "USB DRIVE", C_PANEL, usable ? C_CYAN : C_GRID, 1,
	        pressing(BTN_LOG_USB, s_touch));

	// The note line says why the last press was refused. Blank the row even
	// when there is nothing to say, or a stale reason outlives its cause.
	gfxRect(0, LOG_NOTE_Y, GFX_W, 8, C_BG);
	if (mscNoteActive())
		gfxTextCenter(LOG_NOTE_Y, mscRefusalText(s_mscNote), C_AMBER, 1);
}

/** @brief Touch handling for the logging screen. All three fire on release. */
static void handleLogTouch() {
	if (s_touch.down) s_shown.config = -1;   // keep the pressed look live

	// While a host owns the card, the only control that does anything is the
	// one that takes it back. BACK still works -- leaving the screen does not
	// end the handover, and should not: the copy is still running.
	if (mscGetState() != MscState::Idle) {
		if (tapped(BTN_LOG_TOGGLE, s_touch)) {
			mscRelease();
			s_shown.config = -1;
		} else if (uiBackTapped(&s_touch)) {
			s_logScreen = false;
			invalidateAll();
			gfxFill(C_BG);
		}
		return;
	}

	if (tapped(BTN_LOG_TOGGLE, s_touch)) {
		if (sdLogActive()) sdLogStop();
		else               sdLogStart();
		s_shown.config = -1;
	} else if (tapped(BTN_LOG_RETRY, s_touch)) {
		sdLogRemount();
		s_shown.config = -1;
	} else if (tapped(BTN_LOG_USB, s_touch)) {
		MscRefusal r = mscRequest();
		if (r != MscRefusal::None) {
			s_mscNote = r;
			s_mscNoteUntilMs = millis() + MSC_NOTE_MS;
		}
		s_shown.config = -1;
	} else if (uiBackTapped(&s_touch)) {
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

	// Keep the pressed look live -- and *clear* it. `released` matters as much
	// as `down`, and this has to run before the stepper early-return: the frame
	// a held stepper was let go used to change no cached value, so drawConfig()
	// drew nothing and the button kept its pressed double-frame until something
	// else happened to invalidate the screen.
	if (s_touch.down || s_touch.released) { s_shown.cmdFlash = -1; }

	if (polesDir || maxtDir) return;

	// --- everything else: fires on release, inside, having started inside ---
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
	} else if (uiBackTapped(&s_touch)) {
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
			// and it returns to zero the moment you let go. Measured from the
			// inner edge of the saturation margin, so both rails are reachable
			// without touching the rim. @see THR_EDGE_SAT
			int rel = x - THR_TRACK_X - THR_EDGE_SAT;
			if (rel < 0) rel = 0;
			if (rel > THR_USABLE_W) rel = THR_USABLE_W;
			s_throttle = (uint16_t)((uint32_t)rel * s_maxThrottle / THR_USABLE_W);
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
	//
	// Refused outright while a host owns the card. BACK from the logging screen
	// leaves the handover running -- deliberately, because a copy in progress
	// should not be cancelled by navigating -- so the tester screen is reachable
	// with the card gone, and arming there would spin a motor whose telemetry
	// cannot be recorded and whose interlocks assume a logger that is not
	// listening. @see usb_msc.h
	if (mscHoldsCard(mscGetState())) {
		s_armPressing = false;
	} else if (pressing(BTN_ARM, s_touch)) {
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
	rpmFilterReset(&s_erpmFilter);
	s_erpmShown = 0;
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

	// Once per frame, before anything draws. Inside drawRpm() it would only run
	// when that region repaints -- and that region repaints *because* the value
	// changed, so the filter would only ever be stepped by its own output.
	s_erpmShown = rpmFilterStep(&s_erpmFilter, s_tel.erpm, telemetryAlive(),
	                            RPM_FILTER_SHIFT);

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
