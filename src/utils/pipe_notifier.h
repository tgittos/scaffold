#ifndef PIPE_NOTIFIER_H
#define PIPE_NOTIFIER_H

/**
 * PipeNotifier - Thread-safe pipe-based notification abstraction.
 *
 * Provides a unified interface for inter-thread/inter-process notification
 * using non-blocking pipes. Used by async_executor, message_poller, and
 * subagent approval channels.
 *
 * Thread safety: All operations are thread-safe. Multiple threads can
 * send notifications concurrently, and one thread can receive.
 */

typedef struct {
    int read_fd;   /* Read end of pipe, for select()/poll() */
    int write_fd;  /* Write end of pipe, for sending notifications */
} PipeNotifier;

/**
 * Initialize a pipe notifier.
 * Creates a non-blocking pipe for notification.
 *
 * @param notifier Pointer to notifier structure to initialize
 * @return 0 on success, -1 on failure
 */
int pipe_notifier_init(PipeNotifier* notifier);

/**
 * Destroy a pipe notifier and close file descriptors.
 *
 * @param notifier Notifier to destroy (may be NULL)
 */
void pipe_notifier_destroy(PipeNotifier* notifier);

/**
 * Send a notification event through the pipe.
 * This is non-blocking and thread-safe.
 *
 * @param notifier The notifier
 * @param event Event byte to send (typically a char like 'M' or 'C')
 * @return 0 on success, -1 on failure
 */
int pipe_notifier_send(PipeNotifier* notifier, char event);

/**
 * Receive a notification event from the pipe (non-blocking).
 *
 * @param notifier The notifier
 * @param event Output: the received event byte
 * @return 1 if event received, 0 if no data available, -1 on error
 */
int pipe_notifier_recv(PipeNotifier* notifier, char* event);

/**
 * Get the read file descriptor for use with select()/poll().
 *
 * @param notifier The notifier
 * @return Read fd, or -1 if notifier is NULL
 */
int pipe_notifier_get_read_fd(PipeNotifier* notifier);

/**
 * Drain all pending notifications from the pipe.
 * Useful for clearing accumulated notifications before processing.
 *
 * @param notifier The notifier
 */
void pipe_notifier_drain(PipeNotifier* notifier);

#endif /* PIPE_NOTIFIER_H */
