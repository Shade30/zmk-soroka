/*
 * Soroka 5x5 LED matrix animations.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "matrix_animation.h"

#define STRIP_NODE DT_NODELABEL(led_strip)
#define MATRIX_W 5
#define MATRIX_H 5
#define MATRIX_LED_COUNT (MATRIX_W * MATRIX_H)
#define FRAME_TIME_MS 40
#define MATRIX_SERPENTINE 0
#define SETTINGS_SAVE_DELAY_MS 2000

struct color {
    uint8_t r, g, b;
};

enum color_id {
    COL_BG,
    COL_DIM,
    COL_GREY,
    COL_WHITE,
    COL_RED,
    COL_RED_DIM,
    COL_RUST,
    COL_BLUE,
    COL_BLUE_DIM,
};

static const struct color PALETTE[] = {
    [COL_BG] = {0, 0, 0},         [COL_DIM] = {46, 42, 46},     [COL_GREY] = {116, 110, 114},
    [COL_WHITE] = {226, 222, 218}, [COL_RED] = {212, 32, 36},    [COL_RED_DIM] = {92, 19, 22},
    [COL_RUST] = {170, 60, 30},    [COL_BLUE] = {42, 150, 200},  [COL_BLUE_DIM] = {20, 60, 84},
};

static const uint8_t PAL_GREYS[] = {COL_DIM, COL_GREY};
static const uint8_t PAL_DIM_RED[] = {COL_DIM, COL_RED_DIM};
static const uint8_t PAL_GREY_RED[] = {COL_DIM, COL_GREY, COL_RED_DIM};
static const uint8_t PAL_GREY_BLUE[] = {COL_DIM, COL_GREY, COL_BLUE_DIM};
static const uint8_t PAL_RED_BLUE[] = {COL_DIM, COL_RED_DIM, COL_BLUE_DIM};
static const uint8_t PAL_DIGIT_NOISE[] = {COL_BG, COL_RED, COL_GREY};

static const uint8_t BRIGHTNESS_LEVELS[] = {6, 12, 20, 40, 64, 96, 140, 200, 255};
#define DEFAULT_BRIGHTNESS_LEVEL 3

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct color frame[MATRIX_LED_COUNT];
static struct color scratch[MATRIX_LED_COUNT];
static struct led_rgb pixels[MATRIX_LED_COUNT];

static uint32_t rng_state = 0x5EED1234;

static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static uint32_t rnd_below(uint32_t n) { return rnd() % n; }

static bool chance(uint32_t permille) { return rnd_below(1000) < permille; }

static int rnd_sign(void) { return chance(500) ? -1 : 1; }

static uint32_t hash_permille(uint32_t n) {
    n ^= n * 0x9E3779B1u;
    n ^= n >> 15;
    n *= 0x85EBCA6Bu;
    n ^= n >> 13;
    return n % 1000;
}

static int clamp1(int v) { return CLAMP(v, -1, 1); }

static struct color col(enum color_id id) { return PALETTE[id]; }

static struct color scale(struct color c, uint32_t permille) {
    return (struct color){(uint8_t)(c.r * permille / 1000), (uint8_t)(c.g * permille / 1000),
                          (uint8_t)(c.b * permille / 1000)};
}

static bool lit(struct color c) { return (c.r | c.g | c.b) > 40; }

static void px_set(struct color *f, int x, int y, struct color c) {
    if (x < 0 || y < 0 || x >= MATRIX_W || y >= MATRIX_H) {
        return;
    }
    f[y * MATRIX_W + x] = c;
}

static struct color px_get(const struct color *f, int x, int y) {
    if (x < 0 || y < 0 || x >= MATRIX_W || y >= MATRIX_H) {
        return col(COL_BG);
    }
    return f[y * MATRIX_W + x];
}

static void fill(struct color *f, struct color c) {
    for (int i = 0; i < MATRIX_LED_COUNT; i++) {
        f[i] = c;
    }
}

static void noise(struct color *f, uint32_t density_permille, const uint8_t *pal, size_t pal_len) {
    for (int i = 0; i < MATRIX_LED_COUNT; i++) {
        if (chance(density_permille)) {
            f[i] = col(pal[rnd_below(pal_len)]);
        }
    }
}

static void tear(struct color *f, int rows) {
    memcpy(scratch, f, sizeof(scratch));
    for (int i = 0; i < rows; i++) {
        int y = rnd_below(MATRIX_H), dx = rnd_sign();
        for (int x = 0; x < MATRIX_W; x++) {
            f[y * MATRIX_W + x] = scratch[y * MATRIX_W + (((x - dx) % MATRIX_W) + MATRIX_W) % MATRIX_W];
        }
    }
}

static void ghost(struct color *f, enum color_id id) {
    memcpy(scratch, f, sizeof(scratch));
    int dx = rnd_sign();
    for (int y = 0; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            if (lit(px_get(scratch, x - dx, y)) && !lit(f[y * MATRIX_W + x])) {
                f[y * MATRIX_W + x] = col(id);
            }
        }
    }
}

static const uint8_t FONT[36][5] = {
    ['A' - 'A'] = {0x0E, 0x11, 0x1F, 0x11, 0x11}, ['B' - 'A'] = {0x1E, 0x11, 0x1E, 0x11, 0x1E},
    ['C' - 'A'] = {0x0F, 0x10, 0x10, 0x10, 0x0F}, ['D' - 'A'] = {0x1E, 0x11, 0x11, 0x11, 0x1E},
    ['E' - 'A'] = {0x1F, 0x10, 0x1E, 0x10, 0x1F}, ['F' - 'A'] = {0x1F, 0x10, 0x1E, 0x10, 0x10},
    ['G' - 'A'] = {0x0F, 0x10, 0x13, 0x11, 0x0F}, ['H' - 'A'] = {0x11, 0x11, 0x1F, 0x11, 0x11},
    ['I' - 'A'] = {0x1F, 0x04, 0x04, 0x04, 0x1F}, ['K' - 'A'] = {0x11, 0x12, 0x1C, 0x12, 0x11},
    ['L' - 'A'] = {0x10, 0x10, 0x10, 0x10, 0x1F}, ['M' - 'A'] = {0x11, 0x1B, 0x15, 0x11, 0x11},
    ['N' - 'A'] = {0x11, 0x19, 0x15, 0x13, 0x11}, ['O' - 'A'] = {0x0E, 0x11, 0x11, 0x11, 0x0E},
    ['P' - 'A'] = {0x1E, 0x11, 0x1E, 0x10, 0x10}, ['R' - 'A'] = {0x1E, 0x11, 0x1E, 0x12, 0x11},
    ['S' - 'A'] = {0x0F, 0x10, 0x0E, 0x01, 0x1E}, ['T' - 'A'] = {0x1F, 0x04, 0x04, 0x04, 0x04},
    ['U' - 'A'] = {0x11, 0x11, 0x11, 0x11, 0x0E}, ['W' - 'A'] = {0x11, 0x11, 0x15, 0x1B, 0x11},
    [26 + 0] = {0x0E, 0x11, 0x15, 0x11, 0x0E},    [26 + 1] = {0x04, 0x0C, 0x04, 0x04, 0x0E},
    [26 + 2] = {0x1E, 0x01, 0x0E, 0x10, 0x1F},    [26 + 3] = {0x1E, 0x01, 0x0E, 0x01, 0x1E},
    [26 + 4] = {0x11, 0x11, 0x1F, 0x01, 0x01},    [26 + 5] = {0x1F, 0x10, 0x1E, 0x01, 0x1E},
    [26 + 6] = {0x0F, 0x10, 0x1E, 0x11, 0x0E},    [26 + 7] = {0x1F, 0x01, 0x02, 0x04, 0x04},
    [26 + 8] = {0x0E, 0x11, 0x0E, 0x11, 0x0E},    [26 + 9] = {0x0E, 0x11, 0x0F, 0x01, 0x1E},
};

static const uint8_t *glyph_rows(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return FONT[ch - 'A'];
    }
    if (ch >= '0' && ch <= '9') {
        return FONT[26 + ch - '0'];
    }
    return NULL;
}

static void glyph(struct color *f, char ch, struct color c) {
    const uint8_t *rows = glyph_rows(ch);
    if (rows == NULL) {
        return;
    }
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            if ((rows[y] >> (4 - x)) & 1) {
                px_set(f, x, y, c);
            }
        }
    }
}

static void draw_eye(struct color *f, int gx, int gy, bool solid) {
    px_set(f, 0, 2, col(COL_WHITE));
    px_set(f, 4, 2, col(COL_WHITE));
    if (gx > 0) {
        px_set(f, 1, 2, col(COL_WHITE));
    }
    if (gx < 0) {
        px_set(f, 3, 2, col(COL_WHITE));
    }
    for (int y = 1; y <= 3; y++) {
        for (int x = 1; x <= 3; x++) {
            px_set(f, x + gx, y + gy, col(COL_RED));
        }
    }
    if (!solid) {
        px_set(f, 2 + gx, 2 + gy, col(COL_BG));
    }
}

static void draw_lid(struct color *f) {
    for (int x = 0; x < MATRIX_W; x++) {
        px_set(f, x, 2, col(COL_GREY));
    }
}

enum seg_kind {
    SEG_BLACK,
    SEG_FLASH,
    SEG_BURST,
    SEG_LETTER,
    SEG_LETTER_NOISE,
    SEG_LETTER_TORN,
    SEG_LID,
    SEG_CUSTOM,
};

struct segment {
    uint16_t dur_ms;
    uint8_t kind;
    char ch;
    uint8_t color;
    uint16_t density;
    const uint8_t *pal;
    uint8_t pal_len;
    void (*fn)(uint32_t phase, uint32_t local_ms);
};

#define BLACK(ms) {.dur_ms = ms, .kind = SEG_BLACK}
#define FLASH(ms, c) {.dur_ms = ms, .kind = SEG_FLASH, .color = c}
#define BURST(ms, d, p) {.dur_ms = ms, .kind = SEG_BURST, .density = d, .pal = p, .pal_len = ARRAY_SIZE(p)}
#define LETTER(ms, chr, c) {.dur_ms = ms, .kind = SEG_LETTER, .ch = chr, .color = c}
#define LETTER_NOISE(ms, chr, c, d, p)                                                             \
    {.dur_ms = ms, .kind = SEG_LETTER_NOISE, .ch = chr, .color = c, .density = d, .pal = p, .pal_len = ARRAY_SIZE(p)}
#define LETTER_TORN(ms, chr, c) {.dur_ms = ms, .kind = SEG_LETTER_TORN, .ch = chr, .color = c}
#define LID(ms) {.dur_ms = ms, .kind = SEG_LID}
#define CUSTOM(ms, f) {.dur_ms = ms, .kind = SEG_CUSTOM, .fn = f}
#define GAP BLACK(40)

static void eye_saccades(uint32_t phase, uint32_t local_ms) {
    uint32_t step = local_ms / 220;
    int gx = clamp1(((int)hash_permille(step * 5 + 1) - 500) * 26 / 10000);
    int gy = clamp1(((int)hash_permille(step * 9 + 2) - 500) * 26 / 10000);
    if (chance(140)) {
        gx = clamp1(gx + rnd_sign());
    }
    if (chance(80)) {
        gy = clamp1(gy + rnd_sign());
    }
    draw_eye(frame, gx, gy, false);
    if (chance(100)) {
        tear(frame, 1);
    }
}

static const int8_t SCAN_STOPS[][2] = {{-1, 0}, {0, 0},  {1, 0},  {1, -1}, {0, -1},
                                       {-1, -1}, {-1, 1}, {0, 1}, {1, 1},  {0, 0}};

static void eye_scan(uint32_t phase, uint32_t local_ms) {
    const uint32_t stop_ms = 300;
    uint32_t i = (local_ms / stop_ms) % ARRAY_SIZE(SCAN_STOPS);
    uint32_t prev = (i + ARRAY_SIZE(SCAN_STOPS) - 1) % ARRAY_SIZE(SCAN_STOPS);
    uint32_t ph = (local_ms % stop_ms) * 1000 / stop_ms;
    int gx = SCAN_STOPS[i][0], gy = SCAN_STOPS[i][1];
    int px = SCAN_STOPS[prev][0], py = SCAN_STOPS[prev][1];
    if (ph < 120) {
        draw_eye(frame, chance(500) ? px : px + clamp1(gx - px), py, false);
        tear(frame, 1);
        return;
    }
    draw_eye(frame, clamp1(gx + (chance(80) ? rnd_sign() : 0)), gy, false);
    if (ph > 900 && chance(500)) {
        tear(frame, 1);
    }
}

static void eye_stare(uint32_t phase, uint32_t local_ms) {
    if (chance(60)) {
        fill(frame, col(COL_RED));
        return;
    }
    int gx = chance(350) ? rnd_sign() : 0, gy = chance(250) ? rnd_sign() : 0;
    draw_eye(frame, gx, gy, false);
    if (chance(150)) {
        tear(frame, 1);
    }
}

static void eye_after_blink(uint32_t phase, uint32_t local_ms) {
    draw_eye(frame, 0, 0, false);
    ghost(frame, COL_BLUE_DIM);
    tear(frame, 1);
}

static void eye_solid(uint32_t phase, uint32_t local_ms) {
    draw_eye(frame, 0, 0, true);
    if (phase > 500) {
        ghost(frame, COL_BLUE_DIM);
    }
}

static void eye_solid_torn(uint32_t phase, uint32_t local_ms) {
    draw_eye(frame, 0, 0, true);
    tear(frame, 3);
}

static void rx_sweep(uint32_t phase, uint32_t local_ms) {
    int cur = phase * MATRIX_W / 1000;
    for (int x = 0; x < MATRIX_W; x++) {
        int h = 1 + (rnd_below(5) * rnd_below(5)) / 5;
        for (int y = MATRIX_H - h; y < MATRIX_H; y++) {
            px_set(frame, x, y, col(x == cur ? COL_WHITE : COL_DIM));
        }
    }
}

static void rx_lock(uint32_t phase, uint32_t local_ms) {
    for (int x = 0; x < MATRIX_W; x++) {
        int wave = (x * 2 + local_ms / 60) % 4;
        int h = 2 + (wave < 2 ? wave : 3 - wave) + 1;
        for (int y = MATRIX_H - h; y < MATRIX_H; y++) {
            px_set(frame, x, y, col(y == MATRIX_H - h ? COL_RED : COL_RED_DIM));
        }
    }
}

static void white_static(uint32_t phase, uint32_t local_ms) {
    for (int i = 0; i < MATRIX_LED_COUNT; i++) {
        uint32_t k = 650 + rnd_below(350);
        if (chance(100)) {
            k = 100 + rnd_below(400);
        }
        frame[i] = scale(col(COL_WHITE), k);
    }
    if (chance(40)) {
        tear(frame, 1);
    }
    if (chance(20)) {
        int y = rnd_below(MATRIX_H);
        for (int x = 0; x < MATRIX_W; x++) {
            px_set(frame, x, y, col(COL_DIM));
        }
    }
}

static uint32_t breathe_level(uint32_t phase) {
    int32_t tri = phase < 500 ? (int32_t)phase * 2 : (int32_t)(1000 - phase) * 2;
    return (uint32_t)(tri * tri * (3000 - 2 * tri) / 1000000);
}

static void red_breathe(uint32_t phase, uint32_t local_ms) {
    uint32_t level = breathe_level(phase);
    for (int i = 0; i < MATRIX_LED_COUNT; i++) {
        uint32_t k = level * (800 + rnd_below(400)) / 1000;
        if (chance(30 * level / 1000)) {
            k = 1000;
        }
        frame[i] = scale(col(COL_RED), MIN(k, 1000));
    }
    if (level > 600 && chance(60)) {
        tear(frame, 1);
    }
}

static void red_tail(uint32_t phase, uint32_t local_ms) {
    noise(frame, 150 * (1000 - phase) / 1000, PAL_DIM_RED, ARRAY_SIZE(PAL_DIM_RED));
}

static const struct segment anim_falke[] = {
    LETTER(220, 'A', COL_RED), GAP, LETTER(220, 'C', COL_RED), GAP, LETTER(220, 'H', COL_RED), GAP,
    LETTER(220, 'T', COL_RED), GAP, LETTER(220, 'U', COL_RED), GAP, LETTER(220, 'N', COL_RED), GAP,
    LETTER(220, 'G', COL_RED), GAP, FLASH(80, COL_RED), BURST(250, 500, PAL_GREYS), BLACK(350),
    LETTER(140, 'S', COL_WHITE), GAP, LETTER(140, 'T', COL_WHITE), GAP, LETTER(140, 'I', COL_WHITE), GAP,
    LETTER(140, 'R', COL_WHITE), GAP, LETTER(140, 'B', COL_WHITE), GAP, FLASH(60, COL_RED), BLACK(60),
    FLASH(60, COL_RED), BLACK(400),
    LETTER(200, 'H', COL_RED), GAP, LETTER(200, 'I', COL_RED), GAP, LETTER(200, 'L', COL_RED), GAP,
    LETTER(200, 'F', COL_RED), GAP, LETTER(200, 'E', COL_RED), GAP, BURST(300, 600, PAL_DIM_RED), BLACK(500),
    LETTER(200, 'G', COL_RED), GAP, LETTER(200, 'I', COL_RED), GAP, LETTER(200, 'B', COL_RED), GAP, BLACK(120),
    LETTER(200, 'A', COL_RED), GAP, LETTER(200, 'U', COL_RED), GAP, LETTER(200, 'F', COL_RED), GAP,
    LETTER_TORN(300, 'F', COL_RED_DIM), BLACK(600),
};

#define DIGIT(chr, c) LETTER(320, chr, c), BLACK(80)
#define DIGIT_BAD(chr) LETTER_NOISE(320, chr, COL_WHITE, 200, PAL_DIGIT_NOISE), BLACK(80)
#define GROUP_END BURST(220, 350, PAL_GREYS), BLACK(400)

static const struct segment anim_numbers[] = {
    DIGIT('2', COL_RED), DIGIT('0', COL_WHITE), DIGIT('4', COL_WHITE), DIGIT_BAD('5'), DIGIT('5', COL_WHITE),
    DIGIT('1', COL_WHITE), DIGIT('2', COL_WHITE), GROUP_END,
    DIGIT('2', COL_RED), DIGIT('1', COL_WHITE), DIGIT('5', COL_WHITE), DIGIT_BAD('8'), DIGIT('8', COL_WHITE),
    DIGIT('9', COL_WHITE), GROUP_END,
    DIGIT('6', COL_RED), DIGIT('3', COL_WHITE), DIGIT('0', COL_WHITE), DIGIT_BAD('6'), DIGIT('6', COL_WHITE),
    DIGIT('3', COL_WHITE), GROUP_END,
    DIGIT('3', COL_RED), DIGIT('6', COL_WHITE), DIGIT('6', COL_WHITE), DIGIT_BAD('1'), DIGIT('1', COL_WHITE),
    DIGIT('2', COL_WHITE), GROUP_END,
};

static const struct segment anim_eye_saccades[] = {
    CUSTOM(3000, eye_saccades), LID(70), BLACK(50), LID(70), CUSTOM(250, eye_after_blink),
    BURST(120, 300, PAL_DIM_RED),
};

static const struct segment anim_eye_scan[] = {
    CUSTOM(3000, eye_scan), LID(60), BLACK(40), LID(60), BLACK(40), LID(60), BURST(200, 400, PAL_GREY_RED),
    BLACK(300),
};

static const struct segment anim_eye_stare[] = {
    CUSTOM(1600, eye_stare), LID(50), BLACK(40), LID(50), BLACK(40), LID(50), BLACK(40),
    CUSTOM(400, eye_solid), CUSTOM(200, eye_solid_torn), BURST(250, 500, PAL_RED_BLUE), BLACK(500),
};

static const struct segment anim_lstr[] = {
    LETTER(450, 'L', COL_RUST), BLACK(60), LETTER(450, 'S', COL_RUST), BLACK(60), LETTER(450, 'T', COL_RUST),
    BLACK(60), LETTER(700, 'R', COL_BLUE), LETTER_TORN(250, 'R', COL_BLUE_DIM), BURST(300, 500, PAL_GREY_BLUE),
    BLACK(700),
};

static const struct segment anim_receiver[] = {
    CUSTOM(2500, rx_sweep), CUSTOM(800, rx_lock), BLACK(150),
    LETTER(260, 'W', COL_WHITE), GAP, LETTER(260, 'A', COL_WHITE), GAP, LETTER(260, 'K', COL_WHITE), GAP,
    LETTER(260, 'E', COL_WHITE), GAP, BLACK(120), LETTER(260, 'U', COL_WHITE), GAP, LETTER(260, 'P', COL_WHITE),
    GAP, BURST(300, 450, PAL_GREYS), CUSTOM(1600, rx_sweep),
};

static const struct segment anim_white[] = {CUSTOM(6000, white_static)};

static const struct segment anim_red[] = {CUSTOM(5000, red_breathe), CUSTOM(600, red_tail)};

struct animation {
    const struct segment *segs;
    size_t len;
    uint32_t total_ms;
};

#define ANIMATION(a) {.segs = a, .len = ARRAY_SIZE(a)}

static struct animation animations[MATRIX_ANIMATION_COUNT] = {
    ANIMATION(anim_falke),      ANIMATION(anim_numbers),  ANIMATION(anim_eye_saccades),
    ANIMATION(anim_eye_scan),   ANIMATION(anim_eye_stare), ANIMATION(anim_lstr),
    ANIMATION(anim_receiver),   ANIMATION(anim_white),    ANIMATION(anim_red),
};

static struct {
    uint8_t index;
    uint8_t shuffle;
    uint8_t brightness;
} state = {.brightness = DEFAULT_BRIGHTNESS_LEVEL};

static int64_t anim_start_ms;

static void run_segment(const struct segment *seg, uint32_t local_ms) {
    uint32_t phase = local_ms * 1000 / seg->dur_ms;
    fill(frame, col(COL_BG));
    switch (seg->kind) {
    case SEG_BLACK:
        break;
    case SEG_FLASH:
        fill(frame, col(seg->color));
        break;
    case SEG_BURST:
        noise(frame, seg->density, seg->pal, seg->pal_len);
        if (chance(300)) {
            tear(frame, 1);
        }
        break;
    case SEG_LETTER:
        glyph(frame, seg->ch, col(seg->color));
        if (phase > 850) {
            tear(frame, 1);
        }
        break;
    case SEG_LETTER_NOISE:
        glyph(frame, seg->ch, col(seg->color));
        noise(frame, seg->density, seg->pal, seg->pal_len);
        break;
    case SEG_LETTER_TORN:
        glyph(frame, seg->ch, col(seg->color));
        tear(frame, 2);
        ghost(frame, COL_BLUE_DIM);
        break;
    case SEG_LID:
        draw_lid(frame);
        break;
    case SEG_CUSTOM:
        seg->fn(phase, local_ms);
        break;
    }
}

static void render_frame(const struct animation *anim, uint32_t t_ms) {
    for (size_t i = 0; i < anim->len; i++) {
        if (t_ms < anim->segs[i].dur_ms) {
            run_segment(&anim->segs[i], t_ms);
            return;
        }
        t_ms -= anim->segs[i].dur_ms;
    }
}

static void pick_random_animation(void) {
    uint8_t next = (state.index + 1 + rnd_below(MATRIX_ANIMATION_COUNT - 1)) % MATRIX_ANIMATION_COUNT;
    state.index = next;
}

static size_t physical_index(size_t row, size_t column) {
    if (MATRIX_SERPENTINE && (row % 2U) != 0U) {
        column = MATRIX_W - 1U - column;
    }
    return row * MATRIX_W + column;
}

static void push_frame(void) {
    for (int y = 0; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            struct color c = scale(frame[y * MATRIX_W + x], BRIGHTNESS_LEVELS[state.brightness] * 1000 / 255);
            pixels[physical_index(y, x)] = (struct led_rgb){.r = c.r, .g = c.g, .b = c.b};
        }
    }
    (void)led_strip_update_rgb(strip, pixels, MATRIX_LED_COUNT);
}

static void animation_step(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    const struct animation *anim = &animations[state.index];
    int64_t elapsed = k_uptime_get() - anim_start_ms;
    if (elapsed >= anim->total_ms) {
        anim_start_ms += (elapsed / anim->total_ms) * anim->total_ms;
        elapsed -= (elapsed / anim->total_ms) * anim->total_ms;
        if (state.shuffle) {
            pick_random_animation();
            anim = &animations[state.index];
            anim_start_ms = k_uptime_get();
            elapsed = 0;
        }
    }
    render_frame(anim, (uint32_t)elapsed);
    push_frame();
    k_work_reschedule(dwork, K_MSEC(FRAME_TIME_MS));
}

K_WORK_DELAYABLE_DEFINE(animation_work, animation_step);

#if IS_ENABLED(CONFIG_SETTINGS)

static void save_state(struct k_work *work) {
    settings_save_one("soroka/anim/mode", &state, sizeof(state));
}

K_WORK_DELAYABLE_DEFINE(save_work, save_state);

static void schedule_save(void) { k_work_reschedule(&save_work, K_MSEC(SETTINGS_SAVE_DELAY_MS)); }

static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    if (settings_name_steq(name, "mode", &next) && !next) {
        if (len != sizeof(state)) {
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, &state, sizeof(state));
        if (state.index >= MATRIX_ANIMATION_COUNT) {
            state.index = 0;
        }
        if (state.brightness >= ARRAY_SIZE(BRIGHTNESS_LEVELS)) {
            state.brightness = DEFAULT_BRIGHTNESS_LEVEL;
        }
        return MIN(rc, 0);
    }
    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(soroka_anim, "soroka/anim", NULL, settings_set, NULL, NULL);

#else

static void schedule_save(void) {}

#endif

static void restart_current(void) {
    anim_start_ms = k_uptime_get();
    schedule_save();
}

void matrix_animation_next(void) {
    state.index = (state.index + 1) % MATRIX_ANIMATION_COUNT;
    restart_current();
}

void matrix_animation_prev(void) {
    state.index = (state.index + MATRIX_ANIMATION_COUNT - 1) % MATRIX_ANIMATION_COUNT;
    restart_current();
}

void matrix_animation_select(uint8_t index) {
    if (index >= MATRIX_ANIMATION_COUNT) {
        return;
    }
    state.index = index;
    restart_current();
}

void matrix_animation_brightness_up(void) {
    if (state.brightness + 1 < ARRAY_SIZE(BRIGHTNESS_LEVELS)) {
        state.brightness++;
        schedule_save();
    }
}

void matrix_animation_brightness_down(void) {
    if (state.brightness > 0) {
        state.brightness--;
        schedule_save();
    }
}

void matrix_animation_toggle_shuffle(void) {
    state.shuffle = !state.shuffle;
    schedule_save();
}

static int matrix_animation_init(void) {
    if (!device_is_ready(strip)) {
        return -ENODEV;
    }
    for (size_t i = 0; i < MATRIX_ANIMATION_COUNT; i++) {
        for (size_t s = 0; s < animations[i].len; s++) {
            animations[i].total_ms += animations[i].segs[s].dur_ms;
        }
    }
    rng_state ^= (uint32_t)k_uptime_get_32() | 1u;
    anim_start_ms = k_uptime_get();
    k_work_schedule(&animation_work, K_MSEC(100));
    return 0;
}

SYS_INIT(matrix_animation_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
