#include "goap_state.h"
#include <cJSON.h>
#include <string.h>

bool goap_preconditions_met(const char *preconditions_json, const char *world_state_json) {
    if (preconditions_json == NULL || strcmp(preconditions_json, "[]") == 0) {
        return true;
    }

    cJSON *preconditions = cJSON_Parse(preconditions_json);
    if (preconditions == NULL) return true;
    if (!cJSON_IsArray(preconditions)) { cJSON_Delete(preconditions); return true; }
    if (cJSON_GetArraySize(preconditions) == 0) { cJSON_Delete(preconditions); return true; }

    cJSON *world_state = cJSON_Parse(world_state_json);
    if (world_state == NULL) { cJSON_Delete(preconditions); return false; }

    bool satisfied = true;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, preconditions) {
        if (!cJSON_IsString(item)) continue;
        cJSON *val = cJSON_GetObjectItemCaseSensitive(world_state, item->valuestring);
        if (val == NULL || !cJSON_IsTrue(val)) {
            satisfied = false;
            break;
        }
    }

    cJSON_Delete(preconditions);
    cJSON_Delete(world_state);
    return satisfied;
}

GoapProgress goap_check_progress(const char *goal_state_json, const char *world_state_json) {
    GoapProgress progress = { .complete = false, .satisfied = 0, .total = 0 };

    cJSON *goal_state = goal_state_json ? cJSON_Parse(goal_state_json) : NULL;
    if (goal_state == NULL || !cJSON_IsObject(goal_state)) {
        cJSON_Delete(goal_state);
        return progress;
    }

    cJSON *world_state = world_state_json ? cJSON_Parse(world_state_json) : NULL;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, goal_state) {
        progress.total++;
        cJSON *ws_val = world_state
            ? cJSON_GetObjectItem(world_state, item->string) : NULL;
        if (ws_val != NULL && cJSON_IsTrue(ws_val)) {
            progress.satisfied++;
        }
    }

    progress.complete = (progress.total > 0 && progress.satisfied == progress.total);

    cJSON_Delete(goal_state);
    cJSON_Delete(world_state);
    return progress;
}
