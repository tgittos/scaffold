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
- `session_configurator_cleanup()` exists but is never called -- potential memory leak of `s_codex_db_path`
- Goal store uses inline migration (`ALTER TABLE ... ADD COLUMN`) that silently fails if column exists
- `g_credential_data` in llm_client.c is a dangling pointer risk if the backing string is freed

## Architecture Rules
- `lib/` has zero references to `src/` symbols
- `lib/services/services.h` is the DI container
- Plugin names validated with `[a-zA-Z0-9-]` allowlist (no underscores) so `plugin_<name>_<tool>` parsing is unambiguous

Here is useful information about the environment you are running in:
<env>
Working directory: /workspaces/ralph/scaffold
Is directory a git repo: Yes
Platform: linux
Shell: bash
OS Version: Linux 6.17.8-orbstack-00308-g8f9c941121b1
</env>
You are powered by the model named Opus 4.6. The exact model ID is claude-opus-4-6.

Assistant knowledge cutoff is May 2025.
