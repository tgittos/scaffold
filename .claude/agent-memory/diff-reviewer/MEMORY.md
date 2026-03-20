# Diff Reviewer Memory

Notes:
- Agent threads always have their cwd reset between bash calls, as a result please only use absolute file paths.
- In your final response always share relevant file names and code snippets. Any file paths you return in your response MUST be absolute. Do NOT use relative paths.
- For clear communication with the user the assistant MUST avoid using emojis.
- Do not use a colon before tool calls. Text like "Let me read the file:" followed by a read tool call should just be "Let me read the file." with a period.

## Codebase Patterns
- `SystemPromptParts` struct replaced bare `const char*` system_prompt in API common layer (split prompt caching)
- `LLMProvider` vtable uses `SystemPromptParts*` in build_request_json and build_streaming_request_json
- OAuth2 store uses `mbedtls_platform_zeroize` for sensitive data cleanup
- Plugin system uses JSON-RPC 2.0 over stdin/stdout pipes with `plugin_send_stamped_request` for thread-safe IPC
- FIXED: `session_configurator_cleanup()` is now called in session.c:202 (develop branch)
- Structured logging: `log.h` defines both enum values (LOG_ERROR=0) and macros (LOG_ERROR(...)) -- safe due to C preprocessor self-reference prohibition
- `pthread_once_t` in log.c now correctly initialized with `PTHREAD_ONCE_INIT`
- Goal store uses inline migration (`ALTER TABLE ... ADD COLUMN`) that silently fails if column exists
- `g_credential_data` in llm_client.c is a dangling pointer risk if the backing string is freed
- Tool consolidation: goap, messaging, orchestrator, vector_db collapsed into single operation-dispatch tools via `dispatch_by_operation()` in tool_param_dsl.c
- `load_system_prompt` no longer takes tools_description parameter (removed in this release)
- `iterative_loop_run` now takes `LoopWorkflowState*` third parameter for nudge tracking
- Channel message tracking switched from timestamp-based to rowid-based (migration concern for existing DBs)
- `build_enhanced_prompt_parts` casts away const to set `first_turn_context_injected` (UB concern)
- `ConversationHistory.count` is `size_t` (from DARRAY_DECLARE); use `%zu` format specifier

## Architecture Rules
- `lib/` has zero references to `src/` symbols
- `lib/services/services.h` is the DI container
- Plugin names validated with `[a-zA-Z0-9-]` allowlist (no underscores) so `plugin_<name>_<tool>` parsing is unambiguous
