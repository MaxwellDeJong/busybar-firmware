#include "../busy_i.h"
#include "../helpers/activity_list.h"

#include <gui/modules/menu.h>

#include <busy_timer/time_macros.h>

#include <furi_hal_rtc.h>

#define ACTIVITY_SUMMARY_LEN   (16)
#define ACTIVITY_ICON_PATH_LEN (64)

typedef struct {
    ActivityList* list;
    Menu* front_menu;
    Menu* back_menu;
} BusySceneSetupActivity;

// Build the menu icon path for an activity at a given size, e.g.
// "/ext/apps_assets/busy/images/book_8x8.image". Activities without an icon fall
// back to the default hourglass, matching the other Setup menu rows.
static void busy_scene_setup_activity_format_icon(
    const BusyTimerProfile* profile,
    const char* size_suffix,
    char* out,
    size_t out_size) {
    const char* base =
        (profile->metadata.icon[0] != '\0') ? profile->metadata.icon : "hourglass";
    snprintf(out, out_size, "%s/%s_%s.image", BUSY_ASSETS_PATH("images"), base, size_suffix);
}

// Build a short sub-label describing the timer, e.g. "25/5 x4", "45 min", "Open".
static void busy_scene_setup_activity_format_summary(
    const BusyTimerProfile* profile,
    char* out,
    size_t out_size) {
    const BusyTimerConfig* config = &profile->timer_config;

    if(config->mode == BusyTimerModeInterval) {
        snprintf(
            out,
            out_size,
            "%lu/%lu x%lu",
            (unsigned long)MS_TO_M(config->interval.work_time_ms),
            (unsigned long)MS_TO_M(config->interval.rest_time_ms),
            (unsigned long)config->interval.cycles_count);
    } else if(config->mode == BusyTimerModeSimple) {
        snprintf(
            out, out_size, "%lu min", (unsigned long)MS_TO_M(config->simple.total_time_ms));
    } else {
        strlcpy(out, "Open", out_size);
    }
}

static void busy_scene_setup_activity_menu_callback(uint32_t index, void* context) {
    furi_assert(context);
    BusyApp* instance = context;
    busy_send_custom_event(instance, index);
}

static void busy_scene_setup_activity_on_enter(void* context) {
    furi_assert(context);

    BusyApp* instance = context;
    BusySceneSetupActivity* data =
        scene_manager_get_scene_data(instance->scene_manager, BusyAppSceneIdSetupActivity);

    data->list = activity_list_alloc();
    activity_list_read(data->list);

    // Which activity is loaded in the Custom slot right now? (blocking call is safe
    // here: no timer messages are queued yet.) Used to mark and pre-focus it.
    BusyTimerProfile current;
    busy_timer_get_profile(instance->busy_timer, BusyTimerProfileIdCustom, &current);
    const uint32_t active_index =
        activity_list_find_by_card_id(data->list, current.metadata.card_id);

    const uint32_t count = activity_list_get_count(data->list);

    with_gui(instance->gui, {
        data->front_menu = menu_alloc(instance->front_window);
        data->back_menu = menu_alloc(instance->back_window);

        for(uint32_t i = 0; i < count; ++i) {
            const BusyTimerProfile* activity = activity_list_get_item(data->list, i);
            const bool is_active = (i == active_index);

            char summary[ACTIVITY_SUMMARY_LEN];
            busy_scene_setup_activity_format_summary(activity, summary, sizeof(summary));

            // The active row is marked with a checkmark; the rest show the activity's
            // own icon (or the hourglass fallback). menu_add_item consumes the icon
            // path synchronously, so these stack buffers are safe.
            char front_icon[ACTIVITY_ICON_PATH_LEN];
            char back_icon[ACTIVITY_ICON_PATH_LEN];
            busy_scene_setup_activity_format_icon(activity, "8x8", front_icon, sizeof(front_icon));
            busy_scene_setup_activity_format_icon(
                activity, "11x11", back_icon, sizeof(back_icon));

            menu_add_item(
                data->front_menu,
                activity->metadata.title,
                summary,
                is_active ? SHARED_IMG_PATH("checkmark_front_8x8.image") : front_icon,
                i,
                busy_scene_setup_activity_menu_callback,
                instance);

            menu_add_item(
                data->back_menu,
                activity->metadata.title,
                summary,
                is_active ? SHARED_IMG_PATH("checkmark_back_11x11.image") : back_icon,
                i,
                NULL,
                NULL);
        }

        if(active_index != ACTIVITY_LIST_INVALID_INDEX) {
            menu_set_selected_item_index(data->front_menu, active_index);
            menu_set_selected_item_index(data->back_menu, active_index);
        }

        widget_set_scrollbar_enabled(menu_get_base(data->front_menu), true);
        widget_set_scrollbar_enabled(menu_get_base(data->back_menu), true);
    });
}

static void busy_scene_setup_activity_accept(BusyApp* instance, uint32_t index) {
    BusySceneSetupActivity* data =
        scene_manager_get_scene_data(instance->scene_manager, BusyAppSceneIdSetupActivity);

    const BusyTimerProfile* selected = activity_list_get_item(data->list, index);
    if(selected == NULL) {
        return;
    }

    // Copy by value: the list is freed in on_exit when we navigate away below.
    BusyTimerProfile profile = *selected;

    // Restamp so busy_timer_set_profile's staleness gate accepts a re-selection
    // of the same activity (it rejects timestamps <= the stored one).
    profile.timestamp_ms = furi_hal_rtc_get_timestamp_ms();

    // set_profile (NOT set_preset) so metadata.card_id reaches the streamed snapshot.
    busy_timer_set_profile(instance->busy_timer, BusyTimerProfileIdCustom, &profile);

    // Selecting an activity means we are running the Custom slot, regardless of the
    // physical switch position; this makes busy_get_profile_id() resolve to Custom so
    // the timer scene starts the profile we just wrote.
    instance->preset_id = BusyAppPresetIdCustom;

    // Apply the activity's theme / blanking / smart-home flags to the app.
    busy_set_app_config(instance, &profile.app_config);

    // Same front/back display setup the Start button performs before running.
    with_gui(instance->gui, {
        widget_set_visible(nav_bar_get_base(instance->nav_bar), false);
        widget_set_visible(mirror_card_get_base(instance->timer_card), true);
        mirror_card_set_show_header(instance->timer_card, false);
        mirror_card_set_show_footer(instance->timer_card, false);
    });

    busy_prepare_transition(instance, BusyTransitionTypeSelect);

    // Read the mode from the profile in hand (NOT via a blocking preset query, which
    // would race the set_profile just queued). The destination scene starts the timer.
    if(profile.timer_config.mode == BusyTimerModeInterval) {
        scene_manager_next_scene(instance->scene_manager, BusyAppSceneIdOverview);
    } else {
        scene_manager_next_scene(instance->scene_manager, BusyAppSceneIdTimer);
    }
}

static bool busy_scene_setup_activity_on_event(const SceneManagerEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);

    bool consumed = false;
    BusyApp* instance = context;

    if(event->type == SceneManagerEventTypeCustom) {
        if(event->event < BusyCustomEventIndexMax) {
            busy_scene_setup_activity_accept(instance, event->event);
        }
        consumed = true;

    } else if(event->type == SceneManagerEventTypeBack) {
        busy_pop_location(instance);
    }

    return consumed;
}

static void busy_scene_setup_activity_on_exit(void* context) {
    furi_assert(context);

    BusyApp* instance = context;
    BusySceneSetupActivity* data =
        scene_manager_get_scene_data(instance->scene_manager, BusyAppSceneIdSetupActivity);

    with_gui(instance->gui, {
        menu_free(data->front_menu);
        menu_free(data->back_menu);
    });

    activity_list_free(data->list);
}

const Scene busy_scene_setup_activity = {
    .enter_callback = busy_scene_setup_activity_on_enter,
    .exit_callback = busy_scene_setup_activity_on_exit,
    .event_callback = busy_scene_setup_activity_on_event,
    .data_size = sizeof(BusySceneSetupActivity),
};
