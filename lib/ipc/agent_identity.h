#ifndef LIB_IPC_AGENT_IDENTITY_H
#define LIB_IPC_AGENT_IDENTITY_H

#include <stdbool.h>
#include <pthread.h>

/**
 * AgentIdentity - Thread-safe agent identity management.
 *
 * Provides a unified, thread-safe abstraction for agent identity that was
 * previously scattered across session->session_id, g_agent_id global, and
 * RALPH_PARENT_AGENT_ID environment variable.
 *
 * Thread safety: All public functions are thread-safe. Internal state is
 * protected by mutex.
 */

#define AGENT_ID_MAX_LENGTH 40

typedef struct AgentIdentity {
    char id[AGENT_ID_MAX_LENGTH];        /* This agent's unique ID */
    char parent_id[AGENT_ID_MAX_LENGTH]; /* Parent agent ID (empty if root) */
    bool is_subagent;                    /* True if spawned by another agent */
    pthread_mutex_t mutex;               /* Protects all fields */
} AgentIdentity;

/**
 * Create and initialize an agent identity.
 *
 * @param id This agent's unique identifier
 * @param parent_id Parent agent ID, or NULL if this is a root agent
 * @return Newly allocated identity, or NULL on failure. Caller must free with agent_identity_destroy.
 */
AgentIdentity* agent_identity_create(const char* id, const char* parent_id);

/**
 * Destroy an agent identity and free resources.
 *
 * @param identity Identity to destroy (may be NULL)
 */
void agent_identity_destroy(AgentIdentity* identity);

/**
 * Get this agent's ID (thread-safe copy).
 *
 * @param identity The agent identity
 * @return Allocated copy of ID string. Caller must free. Returns NULL on error.
 */
char* agent_identity_get_id(AgentIdentity* identity);

/**
 * Get parent agent's ID (thread-safe copy).
 *
 * @param identity The agent identity
 * @return Allocated copy of parent ID string, or NULL if no parent. Caller must free.
 */
char* agent_identity_get_parent_id(AgentIdentity* identity);

/**
 * Check if this agent is a subagent (has a parent).
 *
 * @param identity The agent identity
 * @return true if subagent, false if root agent or on error
 */
bool agent_identity_is_subagent(AgentIdentity* identity);

/**
 * Update the agent ID (thread-safe).
 * Typically used during session initialization.
 *
 * @param identity The agent identity
 * @param id New agent ID
 * @return 0 on success, -1 on failure
 */
int agent_identity_set_id(AgentIdentity* identity, const char* id);

/**
 * Update the parent agent ID (thread-safe).
 *
 * @param identity The agent identity
 * @param parent_id New parent ID, or NULL to clear
 * @return 0 on success, -1 on failure
 */
int agent_identity_set_parent_id(AgentIdentity* identity, const char* parent_id);

#endif /* LIB_IPC_AGENT_IDENTITY_H */
