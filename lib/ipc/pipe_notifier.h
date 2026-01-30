/**
 * lib/ipc/pipe_notifier.h - Library wrapper for pipe notifier
 *
 * This header re-exports the pipe notifier implementation from src/
 * through the library API. It provides compatibility between the
 * internal src/ interface and the public lib/ interface.
 *
 * Source implementation: src/utils/pipe_notifier.c
 */

#ifndef LIB_IPC_PIPE_NOTIFIER_H
#define LIB_IPC_PIPE_NOTIFIER_H

/* Re-export the original implementation */
#include "../../src/utils/pipe_notifier.h"

/*
 * Library API aliases (ralph_* prefix)
 * These provide the public API as defined in lib/ralph.h
 */

#define ralph_pipe_notifier_init     pipe_notifier_init
#define ralph_pipe_notifier_destroy  pipe_notifier_destroy
#define ralph_pipe_notifier_send     pipe_notifier_send
#define ralph_pipe_notifier_recv     pipe_notifier_recv
#define ralph_pipe_notifier_get_read_fd  pipe_notifier_get_read_fd
#define ralph_pipe_notifier_drain    pipe_notifier_drain

#endif /* LIB_IPC_PIPE_NOTIFIER_H */
