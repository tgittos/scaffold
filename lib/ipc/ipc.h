/**
 * lib/ipc/ipc.h - Inter-Process Communication module
 *
 * This module provides primitives for inter-agent/inter-process communication:
 * - PipeNotifier: Non-blocking pipe-based event notification
 * - AgentIdentity: Thread-safe agent identity management
 * - MessageStore: SQLite-backed direct and pub/sub messaging
 *
 * These components are the foundation for multi-agent coordination.
 */

#ifndef LIB_IPC_IPC_H
#define LIB_IPC_IPC_H

#include "pipe_notifier.h"
#include "agent_identity.h"
#include "message_store.h"
#include "message_poller.h"

#endif /* LIB_IPC_IPC_H */
