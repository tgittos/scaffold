/**
 * Subagent stub for unit tests that don't need full subagent functionality.
 * Returns NULL from subagent_get_approval_channel() indicating we're not
 * running as a subagent.
 */

#include "policy/approval_gate.h"

/**
 * Stub implementation of subagent_get_approval_channel().
 * Always returns NULL, indicating this process is not a subagent.
 */
ApprovalChannel* subagent_get_approval_channel(void) {
    return NULL;
}
