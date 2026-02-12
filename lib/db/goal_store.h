#ifndef GOAL_STORE_H
#define GOAL_STORE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

typedef enum {
    GOAL_STATUS_PLANNING = 0,
    GOAL_STATUS_ACTIVE = 1,
    GOAL_STATUS_PAUSED = 2,
    GOAL_STATUS_COMPLETED = 3,
    GOAL_STATUS_FAILED = 4
} GoalStatus;

typedef struct {
    char id[40];
    char name[128];
    char* description;
    char* goal_state;
    char* world_state;
    char* summary;
    GoalStatus status;
    char queue_name[64];
    pid_t supervisor_pid;
    int64_t supervisor_started_at;
    time_t created_at;
    time_t updated_at;
} Goal;

typedef struct goal_store goal_store_t;

goal_store_t* goal_store_create(const char* db_path);
void goal_store_destroy(goal_store_t* store);

// out_id must be at least 40 bytes.
int goal_store_insert(goal_store_t* store, const char* name,
                      const char* description, const char* goal_state_json,
                      const char* queue_name, char* out_id);

// Caller owns returned Goal and must free with goal_free().
Goal* goal_store_get(goal_store_t* store, const char* id);

int goal_store_update_status(goal_store_t* store, const char* id, GoalStatus status);
int goal_store_update_world_state(goal_store_t* store, const char* id,
                                   const char* world_state_json);
int goal_store_update_summary(goal_store_t* store, const char* id, const char* summary);
int goal_store_update_supervisor(goal_store_t* store, const char* id,
                                  pid_t pid, int64_t started_at);

// Caller owns returned arrays and must free with goal_free_list().
Goal** goal_store_list_all(goal_store_t* store, size_t* count);
Goal** goal_store_list_by_status(goal_store_t* store, GoalStatus status, size_t* count);

void goal_free(Goal* goal);
void goal_free_list(Goal** goals, size_t count);

const char* goal_status_to_string(GoalStatus status);
GoalStatus goal_status_from_string(const char* status_str);

#endif // GOAL_STORE_H
