#include "process_spawn.h"
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
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

int process_spawn_capture(char *args[], char *buf, size_t buf_size) {
    if (args == NULL || args[0] == NULL || buf == NULL || buf_size == 0) return -1;
    buf[0] = '\0';

    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        /* Close inherited FDs above stderr. */
        long max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd < 0) max_fd = 1024;
        for (int fd = STDERR_FILENO + 1; fd < (int)max_fd; fd++) {
            close(fd);
        }

        execv(args[0], args);
        _exit(127);
    }

    close(pipefd[1]);

    size_t pos = 0;
    char tmp[1024];
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        size_t copy = (size_t)n;
        if (pos + copy >= buf_size - 1) copy = buf_size - 1 - pos;
        if (copy == 0) break;
        memcpy(buf + pos, tmp, copy);
        pos += copy;
    }
    buf[pos] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    return (int)pos;
}
