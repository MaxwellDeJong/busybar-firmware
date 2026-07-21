#include "activity_list.h"

#include "../storage_macros.h"

#include <furi.h>
#include <storage/storage.h>

#include <m-array.h>

#define TAG "BusyActivityList"

#define ACTIVITY_FILE_EXT      ".activity"
#define ACTIVITY_NAME_LEN_MAX  (64)
#define ACTIVITY_FILE_SIZE_MAX (2048)

ARRAY_DEF(ActivityArray, BusyTimerProfile, M_POD_OPLIST)

struct ActivityList {
    ActivityArray_t items;
};

ActivityList* activity_list_alloc(void) {
    ActivityList* instance = malloc(sizeof(ActivityList));
    ActivityArray_init(instance->items);
    return instance;
}

void activity_list_free(ActivityList* instance) {
    furi_assert(instance);
    ActivityArray_clear(instance->items);
    free(instance);
}

static bool activity_list_has_extension(const char* name) {
    const size_t name_len = strlen(name);
    const size_t ext_len = strlen(ACTIVITY_FILE_EXT);
    return name_len > ext_len && strcmp(name + name_len - ext_len, ACTIVITY_FILE_EXT) == 0;
}

// Read one file into a heap buffer and deserialize it into *profile.
// The app stack is only 4 KiB, so the file contents go on the heap.
static bool
    activity_list_read_file(Storage* storage, const char* path, BusyTimerProfile* profile) {
    bool success = false;
    File* file = storage_file_alloc(storage);
    char* buffer = NULL;

    do {
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            FURI_LOG_W(TAG, "Cannot open activity file: %s", path);
            break;
        }

        const uint64_t size = storage_file_size(file);
        if(size == 0 || size > ACTIVITY_FILE_SIZE_MAX) {
            FURI_LOG_W(TAG, "Activity file %s has bad size %llu", path, size);
            break;
        }

        buffer = malloc(size);
        if(storage_file_read(file, buffer, size) != size) {
            FURI_LOG_W(TAG, "Short read on activity file: %s", path);
            break;
        }

        if(!busy_timer_profile_deserialize(profile, buffer, size)) {
            FURI_LOG_W(TAG, "Failed to parse activity file: %s", path);
            break;
        }

        if(!busy_timer_profile_is_valid(profile)) {
            FURI_LOG_W(TAG, "Invalid activity profile in file: %s", path);
            break;
        }

        success = true;
    } while(false);

    if(buffer) {
        free(buffer);
    }
    storage_file_close(file);
    storage_file_free(file);

    return success;
}

// Stable insertion sort by sort_order ascending, then title. The list is tiny
// (a handful of activities), so a simple in-place sort over POD elements is
// clearest; a memcmp-based oplist sort would order by struct bytes, not intent.
static bool activity_list_less(const BusyTimerProfile* a, const BusyTimerProfile* b) {
    if(a->metadata.sort_order != b->metadata.sort_order) {
        return a->metadata.sort_order < b->metadata.sort_order;
    }
    return strcmp(a->metadata.title, b->metadata.title) < 0;
}

static void activity_list_sort(ActivityList* instance) {
    const size_t count = ActivityArray_size(instance->items);

    for(size_t i = 1; i < count; ++i) {
        const BusyTimerProfile key = *ActivityArray_cget(instance->items, i);
        size_t j = i;
        while(j > 0 && activity_list_less(&key, ActivityArray_cget(instance->items, j - 1))) {
            *ActivityArray_get(instance->items, j) = *ActivityArray_cget(instance->items, j - 1);
            --j;
        }
        *ActivityArray_get(instance->items, j) = key;
    }
}

void activity_list_read(ActivityList* instance) {
    furi_assert(instance);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);

    do {
        if(!storage_dir_open(dir, BUSY_ACTIVITIES_DIR)) {
            FURI_LOG_W(TAG, "Cannot open activities dir: %s", BUSY_ACTIVITIES_DIR);
            break;
        }

        FileInfo file_info;
        char file_name[ACTIVITY_NAME_LEN_MAX];

        while(storage_dir_read(dir, &file_info, file_name, sizeof(file_name))) {
            if(file_info.flags & FSF_DIRECTORY) {
                continue;
            }
            if(!activity_list_has_extension(file_name)) {
                continue;
            }

            FuriString* path =
                furi_string_alloc_printf("%s/%s", BUSY_ACTIVITIES_DIR, file_name);

            BusyTimerProfile profile;
            if(activity_list_read_file(storage, furi_string_get_cstr(path), &profile)) {
                ActivityArray_push_back(instance->items, profile);
                FURI_LOG_I(
                    TAG,
                    "Loaded activity '%s' (%s)",
                    profile.metadata.title,
                    profile.metadata.card_id);
            }

            furi_string_free(path);
        }

        storage_dir_close(dir);
    } while(false);

    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);

    activity_list_sort(instance);

    FURI_LOG_I(TAG, "Loaded %lu activities", activity_list_get_count(instance));
}

uint32_t activity_list_get_count(const ActivityList* instance) {
    furi_assert(instance);
    return ActivityArray_size(instance->items);
}

const BusyTimerProfile* activity_list_get_item(const ActivityList* instance, uint32_t index) {
    furi_assert(instance);
    if(index >= ActivityArray_size(instance->items)) {
        return NULL;
    }
    return ActivityArray_cget(instance->items, index);
}

uint32_t activity_list_find_by_card_id(const ActivityList* instance, const char* card_id) {
    furi_assert(instance);
    furi_assert(card_id);

    for(uint32_t i = 0; i < ActivityArray_size(instance->items); ++i) {
        const BusyTimerProfile* profile = ActivityArray_cget(instance->items, i);
        if(strcmp(profile->metadata.card_id, card_id) == 0) {
            return i;
        }
    }

    return ACTIVITY_LIST_INVALID_INDEX;
}
