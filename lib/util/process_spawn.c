#include "process_spawn.h"
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Close every descriptor above stderr so the child does not inherit SQLite
 * connections, pipes or sockets from its parent. */
static void close_inherited_fds(void) {
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0) max_fd = 1024;
    for (int fd = STDERR_FILENO + 1; fd < (int)max_fd; fd++) {
        close(fd);
    }
}

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

        close_inherited_fds();

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

        execvp(args[0], args);
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

pid_t process_spawn_logged(char *const argv[], const char *log_path) {
    if (argv == NULL || argv[0] == NULL || log_path == NULL) return -1;

    /* Fail before forking if the log cannot be written, so the caller sees it. */
    int probe = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (probe < 0) return -1;
    close(probe);

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        /* Detach from the parent's session so the child survives it. */
        setsid();

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        close_inherited_fds();

        execv(argv[0], argv);
        _exit(127);
    }

    return pid;
}
