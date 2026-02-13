#include "process_spawn.h"
#include <fcntl.h>
#include <unistd.h>

int process_spawn_devnull(char *args[], pid_t *out_pid) {
    if (args == NULL || args[0] == NULL || out_pid == NULL) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        /* Close inherited FDs (SQLite connections, pipes, etc.) */
        long max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd < 0) max_fd = 1024;
        for (int fd = STDERR_FILENO + 1; fd < (int)max_fd; fd++) {
            close(fd);
        }

        execv(args[0], args);
        _exit(127);
    }

    *out_pid = pid;
    return 0;
}
