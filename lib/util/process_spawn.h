#ifndef PROCESS_SPAWN_H
#define PROCESS_SPAWN_H

#include <sys/types.h>

/**
 * Spawn a child process by fork/exec with stdout/stderr to /dev/null.
 *
 * @param args  NULL-terminated argument array (args[0] is the executable path)
 * @param out_pid  Output: child PID on success
 * @return 0 on success, -1 on failure
 */
int process_spawn_devnull(char *args[], pid_t *out_pid);

/**
 * Spawn a child process, capture its stdout into buf (NUL-terminated).
 * stderr is discarded. Waits for the child to exit.
 *
 * @param args      NULL-terminated argument array (args[0] is the executable:
 *                  an absolute path, or a bare command name resolved via PATH)
 * @param buf       Buffer to receive stdout
 * @param buf_size  Size of buf (including space for NUL terminator)
 * @return Bytes written to buf (>=0), or -1 on fork/exec failure.
 */
int process_spawn_capture(char *args[], char *buf, size_t buf_size);

#endif /* PROCESS_SPAWN_H */
