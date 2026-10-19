/*
 * ZMK behavior that switches the Soroka LED matrix animation.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_matrix_anim

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <dt-bindings/soroka/anim.h>

#include "matrix_animation.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case ANIM_NEXT:
        matrix_animation_next();
        break;
    case ANIM_PREV:
        matrix_animation_prev();
        break;
    case ANIM_SHUFFLE:
        matrix_animation_toggle_shuffle();
        break;
    default:
        if (binding->param1 >= ANIM_SEL_BASE) {
            matrix_animation_select(binding->param1 - ANIM_SEL_BASE);
        }
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {.display_name = "Next", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = ANIM_NEXT},
    {.display_name = "Previous", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = ANIM_PREV},
    {.display_name = "Toggle shuffle", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = ANIM_SHUFFLE},
    {.display_name = "Select",
     .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
     .range = {.min = ANIM_SEL_BASE, .max = ANIM_SEL_BASE + MATRIX_ANIMATION_COUNT - 1}},
};

static const struct behavior_parameter_metadata_set metadata_sets[] = {
    {.param1_values = param_values, .param1_values_len = ARRAY_SIZE(param_values)},
};

static const struct behavior_parameter_metadata metadata = {
    .sets = metadata_sets,
    .sets_len = ARRAY_SIZE(metadata_sets),
};

#endif

static const struct behavior_driver_api behavior_matrix_anim_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_matrix_anim_driver_api);

#endif
