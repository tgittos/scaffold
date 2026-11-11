#ifndef AUTO_APPROVE_H
#define AUTO_APPROVE_H

struct ToolCall;
typedef struct ToolCall ToolCall;

/**
 * Auto mode's first tier, decided without a model call: shell commands whose
 * every segment is read-only, and file writes that stay inside the git
 * project containing the working directory (excluding .git and protected
 * files).
 *
 * @param tool_call The gated tool call
 * @return 1 when the call may run without review, 0 otherwise
 */
int auto_approve_without_review(const ToolCall *tool_call);

#endif /* AUTO_APPROVE_H */
