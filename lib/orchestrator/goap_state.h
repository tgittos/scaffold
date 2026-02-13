#ifndef GOAP_STATE_H
#define GOAP_STATE_H

#include <stdbool.h>

typedef struct {
    bool complete;
    int satisfied;
    int total;
} GoapProgress;

/**
 * Check if all preconditions (JSON array of string keys) are satisfied
 * in the world state (JSON object with boolean values).
 *
 * Returns true if preconditions is NULL, empty, or all keys are true in world_state.
 */
bool goap_preconditions_met(const char *preconditions_json, const char *world_state_json);

/**
 * Check progress of a goal by comparing goal_state assertions against world_state.
 *
 * goal_state is a JSON object where each key is an assertion.
 * world_state is a JSON object with boolean values.
 * complete is true when every goal_state key exists and is true in world_state.
 */
GoapProgress goap_check_progress(const char *goal_state_json, const char *world_state_json);

#endif /* GOAP_STATE_H */
