# Comment Auditor Memory

## Project Patterns Observed
- Security-sensitive code (lib/auth/, lib/db/oauth2_store.c) generally has good "why" comments explaining threat models, TOCTOU mitigations, and crypto choices.
- Common anti-pattern: `/* Parse JSON */`, `/* Decode base64 */` style narration comments before well-named function calls. Particularly prevalent in jwt_decode.c and codex_provider.c.
- Decoration separators (`/* ===...=== */` and `/* ---...--- */`) used heavily in oauth2_store.c and plugin_protocol.c. These add no information.
- The codebase uses a vtable pattern for providers (OAuth2ProviderOps, LLMProvider) -- comments explaining vtable contract and "why not called" for stub implementations are valuable.
- Header files (.h) have generally good API documentation using @param/@return style, but some newer headers (codex_provider.h, http_form_post.h) lack any documentation.
- `tool_batch_executor.c` uses phase separator banners (`/* === Phase N: ... === */`) — decorative, but the phase breakdown is architecturally meaningful; still redundant with the surrounding code.
- `(void)unused_param;  /* unused parameter */` comments — redundant; the cast already silences the compiler warning. The comment adds nothing.
- `// C++ style` inline comments used alongside `/* C style */` — both appear; no project convention to flag.
- `iterative_loop.c` and `tool_executor.c` use `//` style inline comments — tolerated project-wide, not an error.
- `session.c` line 307-309: valuable "why" comment explaining consolidated `pre_llm_send` hook firing; keep pattern.
- Good "why" comments found: `async_executor.c` line 345-347 explaining interrupt propagation; `message_poller.c` lines 53-55 explaining race condition; `agent.c` init ordering comments (lines 57-58, 84-89). These are the right comment style to replicate.
