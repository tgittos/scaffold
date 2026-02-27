# Documentation Sync Analyzer Memory

Notes:
- Agent threads always have their cwd reset between bash calls, as a result please only use absolute file paths.
- In your final response always share relevant file names and code snippets. Any file paths you return in your response MUST be absolute. Do NOT use relative paths.
- For clear communication with the user the assistant MUST avoid using emojis.
- Do not use a colon before tool calls. Text like "Let me read the file:" followed by a read tool call should just be "Let me read the file." with a period.

## Project Documentation Structure
- ARCHITECTURE.md: Mermaid diagrams + detailed module docs + directory tree (lines 1179-1367)
- CODE_OVERVIEW.md: File-by-file listing of src/, lib/, test/ with descriptions
- AGENTS.md: Near-identical to CLAUDE.md, intended for LLM context injection
- README.md: Public-facing with capabilities list, quick start, links to docs/
- docs/: 10 user-facing guides (getting-started, configuration, cli-reference, etc.)

## Known Documentation Patterns
- ARCHITECTURE.md directory tree (lines 1179-1367) must be kept in sync with actual files
- Module Dependencies diagram (lines 291-347) is a simplified view -- prone to drift
- docs/configuration.md provider detection table and approval gate categories must match code
- docs/cli-reference.md must list all CLI flags from main.c
- Test files in CODE_OVERVIEW.md must match actual test/ directory contents

## Common Drift Areas
- New lib/ subdirectories not added to ARCHITECTURE.md directory tree
- New CLI flags not added to docs/cli-reference.md
- New approval gate categories not added to docs/configuration.md
- New slash commands not added to docs/interactive-mode.md
- Files listed in wrong directories (verified_file_python.c/h: listed in lib/policy/ but lives in src/tools/)
