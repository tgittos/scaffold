# Copywriter Memory

## Product: scaffold
- Single portable C binary compiled with Cosmopolitan (Actually Portable Executables)
- Autonomous AI coding agent: orchestrator -> supervisors (GOAP) -> workers
- Key differentiator: "You're the client, scaffold is the team" -- not a copilot, inverts the human/AI relationship
- README voice: confident, opinionated, slightly provocative. Short paragraphs. No corporate fluff.

## Key Feature: Codex Auth (ChatGPT subscription login)
- `--login` / `--logout` CLI flags
- OAuth2 with PKCE, AES-256-GCM encrypted token storage in SQLite
- Routes through `chatgpt.com/backend-api/codex/responses`
- Positioning: recommended config option, flat rate vs per-token billing
- Code: `lib/auth/openai_login.c`, `lib/auth/openai_oauth_provider.c`, `lib/llm/providers/codex_provider.c`, `lib/db/oauth2_store.c`

## Key files
- README: `/workspaces/ralph/scaffold/README.md`
- Architecture: `/workspaces/ralph/scaffold/ARCHITECTURE.md`
- Code overview: `/workspaces/ralph/scaffold/CODE_OVERVIEW.md`

## Technical claims to verify
- "54 built-in tools" -- count from ARCHITECTURE.md sections: Memory(3) + Subagent(2) + Messaging(6) + Todo(2) + GOAP(10) + Orchestrator(6) + VectorDB(13) + Python(1) + PythonFile(11) + switch_mode(1) = ~55. Close enough, keep "54" unless precise count changes.
- Prompt caching: confirmed in `anthropic_provider.c` (beta header) and `ARCHITECTURE.md` (split system prompt architecture)
- Plugin path: `~/.local/scaffold/plugins/`
