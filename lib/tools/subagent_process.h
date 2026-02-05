/*
 * subagent_process.h - Subagent Process I/O and Lifecycle
 *
 * Functions for reading subagent output, handling process exit,
 * and managing subagent resources. Extracted from subagent_tool.c.
 */

#ifndef SUBAGENT_PROCESS_H
#define SUBAGENT_PROCESS_H

#include "subagent_tool.h"

/*
 * Read available output from a subagent's pipe (non-blocking).
 * Sets pipe to non-blocking mode and reads any available data.
 * Returns total bytes read, or -1 on error.
 */
int read_subagent_output_nonblocking(Subagent *sub);

/*
 * Read all remaining output from a subagent's pipe (blocking).
 * Called when subagent has exited to collect final output.
 * Returns 0 on success, -1 on error.
 */
int read_subagent_output(Subagent *sub);

/*
 * Clean up resources for a single subagent.
 * Closes pipes and frees allocated strings.
 * Requires Services for message store access.
 */
void cleanup_subagent(Subagent *sub, Services *services);

/*
 * Generate a unique subagent ID using random hex characters.
 * Uses /dev/urandom for cryptographic randomness with a time/pid fallback.
 * id_out must be at least SUBAGENT_ID_LENGTH + 1 bytes.
 */
void generate_subagent_id(char *id_out);

/*
 * Convert subagent status enum to string.
 */
const char* subagent_status_to_string(SubagentStatus status);

/*
 * Handle process exit status and update subagent state.
 * Reads any remaining output, then sets status to COMPLETED or FAILED.
 */
void subagent_handle_process_exit(Subagent *sub, int proc_status);

/*
 * Send a completion message to the parent agent.
 * Called by the harness when subagent state changes.
 * Requires Services for message store access.
 */
void subagent_notify_parent(const Subagent *sub, Services *services);

/*
 * Get the path to the current executable.
 * Returns a newly allocated string that must be freed by caller.
 */
char* subagent_get_executable_path(void);

#endif /* SUBAGENT_PROCESS_H */
