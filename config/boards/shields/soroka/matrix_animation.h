/*
 * Soroka 5x5 LED matrix animations.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#define MATRIX_ANIMATION_COUNT 9

/** Switches to the next animation in the list. */
void matrix_animation_next(void);

/** Switches to the previous animation in the list. */
void matrix_animation_prev(void);

/** Selects an animation by index; indexes outside {@code 0..MATRIX_ANIMATION_COUNT-1} are ignored. */
void matrix_animation_select(uint8_t index);

/** Toggles shuffle mode, in which a random animation follows each completed loop. */
void matrix_animation_toggle_shuffle(void);
