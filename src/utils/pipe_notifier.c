#include "pipe_notifier.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int pipe_notifier_init(PipeNotifier* notifier) {
    if (notifier == NULL) {
        return -1;
    }

    notifier->read_fd = -1;
    notifier->write_fd = -1;

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return -1;
    }

    /* Set both ends to non-blocking */
    int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags != -1) {
        fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    }

    flags = fcntl(pipe_fds[1], F_GETFL, 0);
    if (flags != -1) {
        fcntl(pipe_fds[1], F_SETFL, flags | O_NONBLOCK);
    }

    notifier->read_fd = pipe_fds[0];
    notifier->write_fd = pipe_fds[1];

    return 0;
}

void pipe_notifier_destroy(PipeNotifier* notifier) {
    if (notifier == NULL) {
        return;
    }

    if (notifier->read_fd >= 0) {
        close(notifier->read_fd);
        notifier->read_fd = -1;
    }

    if (notifier->write_fd >= 0) {
        close(notifier->write_fd);
        notifier->write_fd = -1;
    }
}

int pipe_notifier_send(PipeNotifier* notifier, char event) {
    if (notifier == NULL || notifier->write_fd < 0) {
        return -1;
    }

    ssize_t written = write(notifier->write_fd, &event, 1);
    if (written != 1) {
        /* EAGAIN/EWOULDBLOCK means pipe is full, but notification will still be visible */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }

    return 0;
}

int pipe_notifier_recv(PipeNotifier* notifier, char* event) {
    if (notifier == NULL || notifier->read_fd < 0 || event == NULL) {
        return -1;
    }

    ssize_t n = read(notifier->read_fd, event, 1);
    if (n == 1) {
        return 1;
    }

    if (n == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;  /* No data available */
    }

    return -1;  /* Error */
}

int pipe_notifier_get_read_fd(PipeNotifier* notifier) {
    if (notifier == NULL) {
        return -1;
    }
    return notifier->read_fd;
}

void pipe_notifier_drain(PipeNotifier* notifier) {
    if (notifier == NULL || notifier->read_fd < 0) {
        return;
    }

    char buf[64];
    while (read(notifier->read_fd, buf, sizeof(buf)) > 0) {
        /* Keep reading until empty */
    }
}
