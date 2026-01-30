/**
 * lib/ipc/message_store.h - Library wrapper for message store
 *
 * This header re-exports the message store implementation from src/
 * through the library API. It provides compatibility between the
 * internal src/ interface and the public lib/ interface.
 *
 * Source implementation: src/db/message_store.c
 */

#ifndef LIB_IPC_MESSAGE_STORE_H
#define LIB_IPC_MESSAGE_STORE_H

/* Re-export the original implementation */
#include "../../src/db/message_store.h"

/*
 * Library API aliases (ralph_* prefix)
 * These provide the public API as defined in lib/ralph.h
 */

#define ralph_message_store_get       message_store_get_instance
#define ralph_message_store_create    message_store_create
#define ralph_message_store_destroy   message_store_destroy
#define ralph_message_send_direct     message_send_direct
#define ralph_message_has_pending     message_has_pending

#endif /* LIB_IPC_MESSAGE_STORE_H */
