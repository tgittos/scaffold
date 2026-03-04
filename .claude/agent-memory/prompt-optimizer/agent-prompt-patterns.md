# Agent Prompt Patterns for Autonomous Coding Agents

## The Bootstrapping Problem
Agents dropped into unfamiliar codebases need an explicit discovery protocol.
Without one, they default to assumptions (usually Python) or ask the user.

Effective bootstrapping protocol:
1. Orient (root listing, read config files)
2. Discover tests (find and run test suite)
3. Discover build (find and run build command)
4. Note conventions
5. Store findings in memory

Key: make it skippable when memory already has the info.

## Task Complexity Classification
Agents need concrete criteria, not vibes. Effective framework:
- Simple: single file, <10 lines, clear fix → just do it
- Medium: multiple files, 10-100 lines → TodoWrite tracking
- Complex: 3+ independent parallel pieces, 10+ edits → GOAP/orchestration
- Default to Simple, escalate when discovered to be larger

## The Hypothesis Step
Adding "form a theory before editing" between Read and Edit dramatically reduces
premature/wrong edits. The agent reads more strategically when it's building a
mental model, not just scanning for the first thing to change.

## Error Recovery Triggers
"Fix and retry" is not a strategy. Effective pattern:
- After 1 failure: diagnose the specific error
- After 3 failures on same approach: step back, reassess hypothesis
- Environment vs code distinction: is it my change or the setup?

## Dynamic Context Injection Risks
Runtime-injected text (todo state, mode instructions, memories) appears AFTER
the system prompt in the context window. Due to recency bias, conflicting
instructions in dynamic context will often override system prompt directives.
Always audit the full injection pipeline.

## Scaffold-Specific Notes
- context_enhancement.c injects todo instructions that conflict with autonomous behavior
- Mode prompts are injected under "# Active Mode Instructions" header
- Memories are injected under "# Relevant Memories" header
- Rolling summary injected under "# Prior Conversation Summary" header
