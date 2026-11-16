#ifndef CHILD_MODE_H
#define CHILD_MODE_H

#include "approval_gate.h"

#include <stddef.h>

/** Largest number of argv entries child_mode_flags() writes. */
#define CHILD_MODE_FLAGS_MAX 3

/**
 * Register the gate config of this process so spawn sites that have no
 * session at hand (the orchestrator) can inherit its approval mode.
 * The pointer must outlive the process's spawning; pass NULL to clear.
 */
void child_mode_set_process_gate(const ApprovalGateConfig *config);

/** The registered gate config, or NULL. */
const ApprovalGateConfig *child_mode_get_process_gate(void);

/** "--yolo", "--auto" or NULL (normal mode) for the given gate config. */
const char *child_mode_mode_flag(const ApprovalGateConfig *config);

/**
 * Resolve a home directory to an absolute path for a child process.
 *
 * A child runs in its own working directory, so a relative --home would name a
 * different directory there. An existing path is resolved with realpath(); one
 * that doesn't exist yet is joined to the current working directory.
 *
 * @param home Home directory as this process knows it
 * @param buf  Receives the absolute path
 * @param size Size of buf
 * @return 0 on success, -1 on bad arguments or when the path doesn't fit
 */
int child_mode_absolute_home(const char *home, char *buf, size_t size);

/**
 * Write the argv entries that make a child use this process's approval mode:
 * "--yolo" when gates are disabled, "--auto" in auto mode, nothing otherwise,
 * then "--home <path>" when a home is set. A NULL config means normal mode.
 *
 * @param config Gate config of the spawning process (may be NULL)
 * @param home   Home directory to pass on (may be NULL or empty)
 * @param out    Receives up to CHILD_MODE_FLAGS_MAX entries
 * @return Number of entries written
 */
int child_mode_flags(const ApprovalGateConfig *config, const char *home,
                     const char *out[CHILD_MODE_FLAGS_MAX]);

#endif
