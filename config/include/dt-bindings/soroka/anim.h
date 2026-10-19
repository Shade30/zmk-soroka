/*
 * Parameters of the Soroka LED matrix animation behavior.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define ANIM_NEXT 0
#define ANIM_PREV 1
#define ANIM_SHUFFLE 2
#define ANIM_BRI_UP 3
#define ANIM_BRI_DOWN 4
#define ANIM_SEL_BASE 16
#define ANIM_SEL(n) (ANIM_SEL_BASE + (n))
