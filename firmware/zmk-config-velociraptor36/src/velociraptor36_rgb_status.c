/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/zmk/rgb.h>
#include <zmk/activity.h>
#include <zmk/behavior.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/rgb_underglow.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define NO_HOST_FLASH_INTERVAL_MS 5000
#define STATUS_MONITOR_INTERVAL_MS 250
#define SUNRISE_FRAME_MS 375

static const struct zmk_led_hsb profile_colors[] = {
    {h : 0, s : 100, b : 25},   /* Profile 0: red */
    {h : 120, s : 100, b : 25}, /* Profile 1: green */
    {h : 240, s : 100, b : 25}, /* Profile 2: blue */
};

static const struct zmk_led_hsb no_host_color = {h : 0, s : 100, b : 25};
static const struct zmk_led_hsb sunrise_colors[] = {
    {h : 300, s : 100, b : 20}, /* magenta */
    {h : 325, s : 100, b : 22}, /* pink */
    {h : 350, s : 100, b : 24}, /* rose */
    {h : 12, s : 100, b : 25},  /* red-orange */
    {h : 24, s : 100, b : 27},  /* orange */
    {h : 36, s : 100, b : 28},  /* amber */
    {h : 48, s : 90, b : 30},   /* gold */
    {h : 24, s : 55, b : 25},   /* peach */
};

static struct k_work_delayable flash_work;
static struct k_work_delayable sunrise_work;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static struct k_work_delayable monitor_work;
#endif
static struct zmk_led_hsb flash_color;
static uint8_t flash_steps_remaining;
static uint8_t sunrise_step;
static uint8_t active_profile;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static uint8_t last_profile = UINT8_MAX;
static bool last_connected;
static int64_t last_no_host_flash_at;
static bool activity_idle;
static bool rgb_enabled_before_idle = true;
#endif

static bool profile_has_color(uint8_t profile) { return profile < ARRAY_SIZE(profile_colors); }

static struct zmk_led_hsb color_for_profile(uint8_t profile) {
    return profile_has_color(profile) ? profile_colors[profile] : no_host_color;
}

static int invoke_rgb(uint32_t param1, uint32_t param2) {
    const struct zmk_behavior_binding binding = {
        .behavior_dev = "rgb_ug",
        .param1 = param1,
        .param2 = param2,
    };

    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    return zmk_behavior_invoke_binding(&binding, event, true);
}

static uint32_t hsb_param(struct zmk_led_hsb color) {
    return RGB_COLOR_HSB_VAL(color.h, color.s, color.b);
}

static void start_flash(struct zmk_led_hsb color, uint8_t flashes);

static void show_color(struct zmk_led_hsb color) {
    invoke_rgb(RGB_EFS_CMD, 0);
    invoke_rgb(RGB_COLOR_HSB_CMD, hsb_param(color));
    invoke_rgb(RGB_ON_CMD, 0);
}

static void show_active_profile(void) { show_color(color_for_profile(active_profile)); }

static bool active_profile_connected(void) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    return zmk_ble_profile_is_connected(active_profile);
#else
    return false;
#endif
}

static void show_idle_status(void) {
    if (activity_idle) {
        invoke_rgb(RGB_OFF_CMD, 0);
        return;
    }

    if (active_profile_connected()) {
        show_active_profile();
    } else {
        invoke_rgb(RGB_OFF_CMD, 0);
    }
}

static void sunrise_handler(struct k_work *work) {
    if (sunrise_step >= ARRAY_SIZE(sunrise_colors)) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        if (active_profile_connected()) {
            show_active_profile();
            return;
        }
#endif
        show_idle_status();
        return;
    }

    show_color(sunrise_colors[sunrise_step++]);
    k_work_reschedule(&sunrise_work, K_MSEC(SUNRISE_FRAME_MS));
}

static void flash_handler(struct k_work *work) {
    if (flash_steps_remaining == 0) {
        show_idle_status();
        return;
    }

    if ((flash_steps_remaining % 2) == 1) {
        invoke_rgb(RGB_OFF_CMD, 0);
    } else {
        show_color(flash_color);
    }

    flash_steps_remaining--;
    k_work_reschedule(&flash_work, K_MSEC(CONFIG_VELOCIRAPTOR36_RGB_STATUS_FLASH_MS));
}

static bool flash_in_progress(void) { return flash_steps_remaining > 0; }

static void start_flash(struct zmk_led_hsb color, uint8_t flashes) {
    k_work_cancel_delayable(&sunrise_work);
    k_work_cancel_delayable(&flash_work);

    flash_color = color;
    flash_steps_remaining = (flashes * 2) - 1;

    show_color(flash_color);
    k_work_reschedule(&flash_work, K_MSEC(CONFIG_VELOCIRAPTOR36_RGB_STATUS_FLASH_MS));
}

static void start_sunrise(void) {
    k_work_cancel_delayable(&flash_work);

    sunrise_step = 0;
    sunrise_handler(NULL);
}

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static void monitor_handler(struct k_work *work) {
    const bool connected = active_profile_connected();
    const int64_t now = k_uptime_get();

    if (activity_idle) {
        goto reschedule;
    }

    if (connected && !last_connected) {
        start_flash(color_for_profile(active_profile), 3);
    } else if (!connected && !flash_in_progress() &&
               now - last_no_host_flash_at >= NO_HOST_FLASH_INTERVAL_MS) {
        last_no_host_flash_at = now;
        start_flash(no_host_color, 2);
    }

    last_connected = connected;
reschedule:
    k_work_reschedule(&monitor_work, K_MSEC(STATUS_MONITOR_INTERVAL_MS));
}

static int rgb_status_listener(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *profile_ev =
        as_zmk_ble_active_profile_changed(eh);
    const struct zmk_split_peripheral_status_changed *split_ev =
        as_zmk_split_peripheral_status_changed(eh);
    const struct zmk_activity_state_changed *activity_ev = as_zmk_activity_state_changed(eh);

    if (activity_ev != NULL) {
        if (activity_ev->state == ZMK_ACTIVITY_ACTIVE) {
            if (activity_idle) {
                activity_idle = false;
                last_no_host_flash_at = k_uptime_get();

                if (rgb_enabled_before_idle) {
                    invoke_rgb(RGB_ON_CMD, 0);
                }
            }
        } else if (!activity_idle) {
            bool enabled = false;
            zmk_rgb_underglow_get_state(&enabled);
            rgb_enabled_before_idle = enabled;
            activity_idle = true;

            k_work_cancel_delayable(&flash_work);
            k_work_cancel_delayable(&sunrise_work);
            invoke_rgb(RGB_OFF_CMD, 0);
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

    if (split_ev != NULL) {
        if (split_ev->connected) {
            if (sunrise_step > 0 && sunrise_step <= ARRAY_SIZE(sunrise_colors)) {
                show_color(sunrise_colors[sunrise_step - 1]);
            } else {
                show_idle_status();
            }
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

    if (profile_ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const uint8_t profile = profile_ev->index;
    const bool connected = zmk_ble_profile_is_connected(profile);
    const bool profile_changed = profile != last_profile;

    active_profile = profile;
    last_profile = profile;
    last_connected = connected;

    if (connected && profile_has_color(profile)) {
        start_flash(color_for_profile(profile), 3);
    } else if (profile_changed && profile_has_color(profile)) {
        start_flash(color_for_profile(profile), 2);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(velociraptor36_rgb_status, rgb_status_listener);
ZMK_SUBSCRIPTION(velociraptor36_rgb_status, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(velociraptor36_rgb_status, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(velociraptor36_rgb_status, zmk_split_peripheral_status_changed);
#endif

static int rgb_status_init(void) {
    k_work_init_delayable(&flash_work, flash_handler);
    k_work_init_delayable(&sunrise_work, sunrise_handler);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    k_work_init_delayable(&monitor_work, monitor_handler);

    active_profile = zmk_ble_active_profile_index();
    last_profile = active_profile;
    last_connected = zmk_ble_profile_is_connected(active_profile);
    last_no_host_flash_at = k_uptime_get();

    start_sunrise();
    k_work_schedule(&monitor_work, K_MSEC(STATUS_MONITOR_INTERVAL_MS));
#else
    start_sunrise();
#endif

    return 0;
}

SYS_INIT(rgb_status_init, APPLICATION, CONFIG_VELOCIRAPTOR36_RGB_STATUS_INIT_PRIORITY);
