/**
 * @file ui_widgets.h
 * @brief One button, one header, one row. Every screen draws through these.
 *
 * This file exists because four screens had grown four answers to the same
 * questions, and the differences were not decisions — nobody chose a 6 px
 * corner radius for the main screen and a 5 px one for AM32, or a 26 px header
 * on SETTINGS, a 30 px one on SETUP and a 50 px one on AM32. They were three
 * copies of the same twelve-line function drifting apart one edit at a time,
 * and the most visible symptom was BACK: top-right on two screens, a thin strip
 * along the bottom on two others, and nowhere at all on the main one.
 *
 * The rules the copies now share, and why each is the way it is:
 *
 * - **BACK is always the same rectangle in the header, top right.** Its
 *   position is a habit, and a habit that is wrong half the time is worse than
 *   no habit. @see UI_BACK_X
 * - **A pressed button is drawn additively** — a brighter frame and the label
 *   nudged — never a swapped fill. Buttons here pick a label colour to suit
 *   their own fill (white on red, cyan on panel, background on a flash), so a
 *   pressed state that replaced the fill would have to know about all of them
 *   to stay legible. Adding to what is there cannot break any pairing.
 * - **Touch targets have a floor.** @ref UI_TAP_MIN is what a fingertip needs;
 *   the 18 px BACK strips this replaces were below it.
 *
 * Hit testing is not duplicated here: it is inputPressing() and inputTapped()
 * in ui_input.h, and the wrappers below only spare callers from spelling out
 * four members of a rectangle they already have.
 *
 * @see ui_input.h for the tap, press and repeat rules these are drawn from.
 */

#pragma once

#include <stdint.h>

struct TouchState;

/**
 * @defgroup uiw_metrics Shared metrics
 * @brief The numbers that were four different numbers.
 * @{
 */
/** @brief Height of the header band on every screen. */
#define UI_HDR_H        30
/** @brief Left margin for titles, labels and the first control in a row. */
#define UI_MARGIN        8
/** @brief Corner radius for every button and chip. */
#define UI_RADIUS        5
/** @brief Smallest side of a touch target that a fingertip reliably hits. */
#define UI_TAP_MIN      22

/** @brief BACK button size. Sized by @ref UI_TAP_MIN, not by its label. */
#define UI_BACK_W       50
#define UI_BACK_H       UI_TAP_MIN
/** @brief BACK button position: top right of the header, on every screen. */
#define UI_BACK_X       (240 - UI_BACK_W - 6)
#define UI_BACK_Y       ((UI_HDR_H - UI_BACK_H) / 2)

/**
 * @brief Top of the optional strip under the header.
 *
 * Where a screen puts the one or two things that qualify what the title says —
 * AM32's device name, SETTINGS' EDT chip and unsaved marker. Below the header
 * rather than inside it, because the header is full: a title at scale 2 and a
 * BACK button leave no room for a second line that is not cramped.
 */
#define UI_STRIP_Y      (UI_HDR_H + 4)
/** @brief Height of that strip. Sized to hold a chip. */
#define UI_STRIP_H      16
/** @brief First row a screen's own content may use. */
#define UI_BODY_Y       (UI_STRIP_Y + UI_STRIP_H + 6)
/** @} */

static_assert(UI_BACK_H <= UI_HDR_H, "BACK does not fit in the header band");
static_assert(UI_BACK_W >= UI_TAP_MIN, "BACK is narrower than a fingertip");

/** @brief A touch target and its drawn rectangle, which are always the same. */
struct UiRect {
	int16_t x, y, w, h;
};

/**
 * @brief True while @p r is touched by a press that began inside it.
 * @param r Rectangle.
 * @param t This frame's touch snapshot; null is treated as no touch.
 * @return true for pressed-state drawing and held repeats.
 */
bool uiPressing(const UiRect &r, const TouchState *t);

/**
 * @brief True on the frame a tap of @p r completes.
 * @param r Rectangle.
 * @param t This frame's touch snapshot; null is treated as no touch.
 * @return true exactly once per completed tap.
 */
bool uiTapped(const UiRect &r, const TouchState *t);

/**
 * @brief Draw a rounded button with its label centred.
 *
 * @param r       Rectangle.
 * @param label   Text, centred.
 * @param fill    Resting fill colour.
 * @param fg      Label colour, chosen against @p fill.
 * @param scale   Text scale.
 * @param pressed True while a finger is on it, from uiPressing().
 */
void uiButton(const UiRect &r, const char *label, uint16_t fill, uint16_t fg,
              int scale, bool pressed);

/**
 * @brief Draw the header band: title on the left, BACK on the right.
 *
 * Draws BACK on every screen that has a header, which is what stops it being a
 * control that exists on some of them and is remembered on all of them.
 *
 * @param title Screen name, drawn at scale 2.
 * @param t     This frame's touch snapshot, for BACK's pressed state. May be null.
 */
void uiHeader(const char *title, const TouchState *t);

/** @brief Rectangle of the header's BACK button. @return The shared target. */
UiRect uiBackRect();

/**
 * @brief True on the frame the header's BACK button is tapped.
 * @param t This frame's touch snapshot; null is treated as no touch.
 * @return true exactly once per completed tap.
 */
bool uiBackTapped(const TouchState *t);

/**
 * @brief Clear the strip under the header, ready for uiStripText() or uiChip().
 *
 * Separate from the two so a screen can put both a caption and a chip on it
 * without one of them wiping the other.
 */
void uiStripClear();

/**
 * @brief Left-aligned caption on the strip under the header.
 * @param text Caption.
 * @param col  Colour.
 */
void uiStripText(const char *text, uint16_t col);

/**
 * @brief Right-aligned pill badge on the strip under the header.
 *
 * For a fact worth reading at a glance and never worth tapping — EDT on or off,
 * which board this is. Deliberately not button-shaped: nothing here responds to
 * a press, and a control that looks tappable and is not gets tapped.
 *
 * @param text Label, drawn at scale 1.
 * @param fill Pill colour. The label is always @ref C_ONACCENT over it.
 */
void uiChip(const char *text, uint16_t fill);

/**
 * @brief One label / value row: caption left, value right-aligned.
 *
 * @param y     Row top.
 * @param h     Row height. The row's background is cleared over it.
 * @param label Left-hand caption, always scale 1.
 * @param value Right-aligned value.
 * @param vcol  Value colour.
 * @param scale Value text scale.
 */
void uiRow(int y, int h, const char *label, const char *value, uint16_t vcol,
           int scale);
