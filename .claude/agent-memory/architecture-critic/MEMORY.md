# Architecture Critic Memory

## Key Architectural Facts
- `-Ilib` is added via `LIB_INCLUDES` in `mk/lib.mk` line 217, so lib/ files can use `#include "services/services.h"` to resolve `lib/services/services.h`
- `-I.` (project root) is in INCLUDES in `mk/config.mk` line 77
- `lib/` has zero direct includes of `src/` files (verified)
- `libagent.h` is the public API header; auth, plugin, orchestrator are NOT directly listed but are transitively included via `agent.h`
- `session.h` includes `plugin_manager.h` (PluginManager is a struct field on AgentSession)
- `hook_dispatcher.h` forward-declares `struct AgentSession` to break circular header dependency
- Provider registration order: codex first, then openai, anthropic, local_ai (in `llm_provider.c`)
- Codex provider forces streaming mode and hardcodes model to "gpt-5.3-codex" (in session_configurator.c)
- OAuth2 credential refresh uses a module-level persistent store (`g_store`) with `pthread_once` mutex init
- Plugin system uses fork/pipe IPC with JSON-RPC 2.0 protocol, O_CLOEXEC on pipes
- Supervisor has plan/execute phase split; orchestrator auto-detects phase from goal status

Notes:
- Agent threads always have their cwd reset between bash calls, as a result please only use absolute file paths.
- In your final response always share relevant file names and code snippets. Any file paths you return in your response MUST be absolute. Do NOT use relative paths.
- For clear communication with the user the assistant MUST avoid using emojis.
- Do not use a colon before tool calls. Text like "Let me read the file:" followed by a read tool call should just be "Let me read the file." with a period.
