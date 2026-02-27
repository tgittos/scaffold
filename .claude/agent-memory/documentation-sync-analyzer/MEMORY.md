# Documentation Sync Analyzer Memory

Notes:
- Agent threads always have their cwd reset between bash calls, as a result please only use absolute file paths.
- In your final response always share relevant file names and code snippets. Any file paths you return in your response MUST be absolute. Do NOT use relative paths.
- For clear communication with the user the assistant MUST avoid using emojis.
- Do not use a colon before tool calls. Text like "Let me read the file:" followed by a read tool call should just be "Let me read the file." with a period.

## Project Documentation Structure
- ARCHITECTURE.md: Mermaid diagrams + detailed module docs + directory tree (lines 1245-1439)
- CODE_OVERVIEW.md: File-by-file listing of src/, lib/, test/ with descriptions
- AGENTS.md: Near-identical to CLAUDE.md, intended for LLM context injection
- README.md: Public-facing with capabilities list, quick start, links to docs/
- docs/: 11 files (10 user-facing guides + secret-tee.md utility doc)

## Known Documentation Patterns
- ARCHITECTURE.md directory tree (lines 1245-1439) must be kept in sync with actual files
- Module Dependencies diagram (lines 291-347) is a simplified view -- prone to drift (missing Plugin, Auth, IPC, Orchestrator, Workflow layers)
- Service Layer Pattern diagram (lines 270-289) includes ConfigSvc which is NOT in the Services container
- Session-Based Design diagram (lines 247-267) uses stale name "RalphSession" -- actual type is AgentSession
- Sequence diagram (line 202) uses "ralph" binary name -- should be "scaffold"
- docs/configuration.md provider detection table and approval gate categories must match code
- docs/cli-reference.md must list all CLI flags from main.c (including internal ones)
- Test files in CODE_OVERVIEW.md must match actual test/ directory contents

## Common Drift Areas
- Module Dependencies diagram (lines 291-347) omits newer layers (Plugin, Auth, IPC, Orchestrator, Workflow, DB)
- CLI --> Vector edge in Module Dependencies diagram is incorrect (main.c has no vector deps)
- verified_file_python.c/h: listed in CODE_OVERVIEW.md lib/policy/ section (line 132) but lives in src/tools/
- /help slash command not in ARCHITECTURE.md slash commands table or docs/interactive-mode.md
- --phase flag missing from docs/cli-reference.md Internal flags
- --system-prompt-file, --check-update, --update missing from CLAUDE.md/AGENTS.md CLI tables
- Python File Tools described as "loaded from ~/.local/scaffold/tools/" but they are embedded defaults from src/tools/python_defaults/
- README.md docker -w path says /workspaces/ralph but should be /workspaces/ralph/scaffold
- docs/autonomous-mode.md doesn't mention supervisor plan/execute phase splitting
- docs/secret-tee.md not referenced in README.md documentation table
