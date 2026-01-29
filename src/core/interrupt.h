#ifndef RALPH_INTERRUPT_H
#define RALPH_INTERRUPT_H

/**
 * Interrupt handling module for graceful Ctrl+C cancellation.
 *
 * Provides cooperative cancellation: long-running operations periodically
 * check the interrupt flag and clean up gracefully before returning.
 */

/**
 * Install SIGINT handler for cooperative cancellation.
 * Saves the previous handler for restoration on cleanup.
 * Uses sigaction() without SA_RESTART so blocking calls are interruptible.
 *
 * Returns 0 on success, -1 on failure.
 */
int interrupt_init(void);

/**
 * Restore original SIGINT handler.
 * Safe to call multiple times or without prior init.
 */
void interrupt_cleanup(void);

/**
 * Check if Ctrl+C was pressed.
 *
 * Returns 1 if interrupt is pending, 0 otherwise.
 */
int interrupt_pending(void);

/**
 * Reset the interrupt flag for the next iteration.
 * Call at the start of each main loop iteration.
 */
void interrupt_clear(void);

/**
 * Mark that cleanup is in progress.
 * Used to signal to nested interrupt checks that cancellation
 * is already being handled.
 */
void interrupt_acknowledge(void);

#endif
