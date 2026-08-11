/**
 * @file ui_lcars.h
 * @brief LCARS (Star Trek) themed drawing helpers for the DShot tester UI.
 *
 * When UI_THEME == 1, the main UI calls these functions instead of the default
 * drawing code. The layout and touch logic remain unchanged; only the visual
 * style is different.
 */

#pragma once

#include <stdint.h>
#include "config.h"
#include "gfx.h"

#if UI_THEME == 1

/**
 * @defgroup lcars_palette LCARS colour palette (RGB565)
 * @{
 */
#define LC_BG       rgb(0, 0, 0)        /**< Black background. */
#define LC_TAN      rgb(255, 153, 102)  /**< Peach/tan — primary frame. */
#define LC_PURPLE   rgb(204, 153, 204)  /**< Lavender — secondary frame. */
#define LC_BLUE     rgb(153, 153, 255)  /**< Periwinkle — data fields. */
#define LC_ORANGE   rgb(255, 187, 85)   /**< Orange — highlights. */
#define LC_RED      rgb(224, 80, 80)    /**< Alert red. */
#define LC_GREEN    rgb(85, 221, 85)    /**< OK green. */
#define LC_CYAN     rgb(102, 204, 255)  /**< Informational. */
#define LC_WHITE    rgb(255, 255, 255)  /**< Text white. */
#define LC_DKBLUE   rgb(51, 51, 102)    /**< Dark fill. */
#define LC_TEXT     rgb(255, 204, 153)  /**< Primary text on dark bg. */
#define LC_DIM      rgb(153, 102, 51)   /**< Dim label. */
/** @} */

/**
 * @brief Draw the LCARS frame border around the screen edges.
 *
 * The canonical LCARS look has a thick side elbow on the left that connects
 * to a horizontal header bar. This draws that frame, leaving the interior
 * clear for content.
 */
void lcarsDrawFrame();

/**
 * @brief LCARS-styled button: pill-shaped with LCARS colours.
 * @param x,y   Position.
 * @param w,h   Size.
 * @param label Button text.
 * @param fill  Fill colour.
 * @param fg    Text colour.
 * @param scale Text scale.
 */
void lcarsBtn(int x, int y, int w, int h, const char *label,
              uint16_t fill, uint16_t fg, int scale);

/**
 * @brief LCARS-styled telemetry tile with header sweep.
 */
void lcarsLabelled(int x, int y, int w, int h, const char *label,
                   const char *value, uint16_t vcol);

/**
 * @brief Draw the LCARS splash screen.
 */
void lcarsDrawSplash();

#endif // UI_THEME == 1
