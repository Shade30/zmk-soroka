# Soroka ZMK firmware

This repository builds wired USB firmware for the
[Soroka](https://github.com/kapee1/soroka/) keyboard on a SparkFun Pro Micro
RP2040-compatible controller. The encoder and the 5x5 WS2812 RGB matrix are
enabled. The matrix shows a small heart animation after power-on.
ZMK Studio is available over the same USB connection and does not require an
unlock key.

GitHub Actions is pinned to the stable ZMK `v0.3` release. Download the UF2
artifact from a successful workflow run, enter the RP2040 bootloader, and copy
the UF2 file to the mounted `RPI-RP2` drive.

## LED matrix animations

The 5x5 WS2812 matrix plays short animations inspired by SIGNALIS, written
in `config/boards/shields/soroka/matrix_animation.c`. One glyph is shown at a
time, interleaved with row tears, noise and flashes:

| # | Animation | What it shows |
|---|-----------|---------------|
| 0 | ACHTUNG / STIRB | Falke's text flashes, one letter at a time |
| 1 | Number station | S-23 radio codes digit by digit |
| 2 | Red eye, saccades | Gaze jumping in all directions |
| 3 | Red eye, scan | Gaze walking around the matrix |
| 4 | Red eye, stare | Trembling stare, rapid blinks |
| 5 | LSTR | The unit code, red letters and a blue R |
| 6 | Receiver sweep | Spectrum noise, signal lock, WAKE UP |
| 7 | White static | Steady white with grain |
| 8 | Red breathe | Red fading in and out with noise |

The `&anim` behavior switches between them from the keymap:
`&anim ANIM_NEXT`, `&anim ANIM_PREV`, `&anim ANIM_SHUFFLE` (toggle random
order after every loop) and `&anim ANIM_SEL(n)` for a fixed index. The
constants come from `config/include/dt-bindings/soroka/anim.h`. The selected
animation and the shuffle flag persist across reboots. `&ext_power EP_TOG`
switches the matrix power off and on.

Brightness is capped by `MATRIX_BRIGHTNESS` in `matrix_animation.c`. If the
image has every second row mirrored, set `MATRIX_SERPENTINE` to `1`.

## Choose physical layout

Supported layouts are defined under `config/boards/shields/soroka/layouts`:

- `default_layout`: one 7U space bar (the shield default);
- `split_space_1_225_275_1_layout`: 1U + 2.25U + 2.75U + 1U split space;
- `split_space_3_1_3_layout`: 3U + 1U + 3U split space.

To switch, choose the layout in your keymap and lay the bindings out for it:

```
/ {
    chosen {
        zmk,physical-layout = &split_space_3_1_3_layout;
    };
};
```

![Physical Layouts](/kle/soroka_matrix.png)
