/**
 * lib/ipc/agent_identity.h - Library wrapper for agent identity
 *
 * This header re-exports the agent identity implementation from src/
 * through the library API. It provides compatibility between the
 * internal src/ interface and the public lib/ interface.
 *
 * Source implementation: src/core/agent_identity.c
 */

#ifndef LIB_IPC_AGENT_IDENTITY_H
#define LIB_IPC_AGENT_IDENTITY_H

/* Re-export the original implementation */
#include "../../src/core/agent_identity.h"

/*
 * Library API aliases (ralph_* prefix)
 * These provide the public API as defined in lib/ralph.h
 */

#define RALPH_AGENT_ID_MAX_LENGTH  AGENT_ID_MAX_LENGTH

#define ralph_agent_identity_create       agent_identity_create
#define ralph_agent_identity_destroy      agent_identity_destroy
#define ralph_agent_identity_get_id       agent_identity_get_id
#define ralph_agent_identity_get_parent_id  agent_identity_get_parent_id
#define ralph_agent_identity_is_subagent  agent_identity_is_subagent

#endif /* LIB_IPC_AGENT_IDENTITY_H */
