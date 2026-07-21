/**
 * @file activity_list.h
 * @brief Loads and holds the list of on-device activities.
 *
 * An activity is a serialized BusyTimerProfile stored as a `*.activity` file in
 * BUSY_ACTIVITIES_DIR. activity_list_read() walks that directory, deserializes
 * and validates each file, and keeps the results sorted by sort_order then
 * title. The Activity setup scene builds its picker menu from this list.
 */
#pragma once

#include <stdint.h>

#include <busy_timer/busy_timer_profile.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACTIVITY_LIST_INVALID_INDEX UINT32_MAX

typedef struct ActivityList ActivityList;

/** @brief Allocate an empty activity list. */
ActivityList* activity_list_alloc(void);

/** @brief Free an activity list. */
void activity_list_free(ActivityList* instance);

/**
 * @brief Populate the list from BUSY_ACTIVITIES_DIR.
 *
 * Reads every `*.activity` file, keeping only those that deserialize into a
 * valid profile. Invalid or unreadable files are skipped with a warning log.
 * The list is sorted (sort_order ascending, then title) when done. Existing
 * contents are left in place; call on a freshly allocated list.
 */
void activity_list_read(ActivityList* instance);

/** @brief Number of activities in the list. */
uint32_t activity_list_get_count(const ActivityList* instance);

/**
 * @brief Get the activity at @p index.
 * @returns pointer to the stored profile, or NULL if @p index is out of range.
 */
const BusyTimerProfile* activity_list_get_item(const ActivityList* instance, uint32_t index);

/**
 * @brief Find the index of the activity whose card_id matches @p card_id.
 * @returns the index, or ACTIVITY_LIST_INVALID_INDEX if none matches.
 */
uint32_t activity_list_find_by_card_id(const ActivityList* instance, const char* card_id);

#ifdef __cplusplus
}
#endif
