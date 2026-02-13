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

#endif /* PROCESS_SPAWN_H */
