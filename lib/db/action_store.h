#ifndef ACTION_STORE_H
#define ACTION_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef enum {
    ACTION_STATUS_PENDING = 0,
    ACTION_STATUS_RUNNING = 1,
    ACTION_STATUS_COMPLETED = 2,
    ACTION_STATUS_FAILED = 3,
    ACTION_STATUS_SKIPPED = 4
} ActionStatus;

typedef struct {
    char id[40];
    char goal_id[40];
    char parent_action_id[40];
    char work_item_id[40];
    char* description;
    char* preconditions;
    char* effects;
    bool is_compound;
    ActionStatus status;
    char role[32];
    char* result;
    int attempt_count;
    time_t created_at;
    time_t updated_at;
} Action;

typedef struct action_store action_store_t;
typedef struct sqlite_dal sqlite_dal_t;

action_store_t* action_store_create(const char* db_path);
action_store_t* action_store_create_with_dal(sqlite_dal_t* dal);
void action_store_destroy(action_store_t* store);

// parent_action_id may be NULL for top-level actions.
// role may be NULL (defaults to "implementation").
// out_id must be at least 40 bytes.
int action_store_insert(action_store_t* store, const char* goal_id,
                        const char* parent_action_id, const char* description,
                        const char* preconditions_json, const char* effects_json,
                        bool is_compound, const char* role, char* out_id);

// Caller owns returned Action and must free with action_free().
Action* action_store_get(action_store_t* store, const char* id);

int action_store_update_status(action_store_t* store, const char* id,
                               ActionStatus status, const char* result);

int action_store_update_work_item(action_store_t* store, const char* id,
                                   const char* work_item_id);

// Fetch all PENDING actions whose preconditions are satisfied by world_state.
// Precondition checking is done in C after fetching all pending actions.
// Caller owns returned array and must free with action_free_list().
Action** action_store_list_ready(action_store_t* store, const char* goal_id,
                                 const char* world_state_json, size_t* count);

// Caller owns returned array and must free with action_free_list().
Action** action_store_list_by_goal(action_store_t* store, const char* goal_id,
                                    size_t* count);

// List direct children of a compound action.
// Caller owns returned array and must free with action_free_list().
Action** action_store_list_children(action_store_t* store,
                                     const char* parent_action_id, size_t* count);

// List all RUNNING actions for a goal.
// Caller owns returned array and must free with action_free_list().
Action** action_store_list_running(action_store_t* store, const char* goal_id,
                                    size_t* count);

int action_store_count_by_status(action_store_t* store, const char* goal_id,
                                  ActionStatus status);

// Mark all PENDING actions for a goal as SKIPPED.
int action_store_skip_pending(action_store_t* store, const char* goal_id);

void action_free(Action* action);
void action_free_list(Action** actions, size_t count);

const char* action_status_to_string(ActionStatus status);
ActionStatus action_status_from_string(const char* status_str);

#endif // ACTION_STORE_H
