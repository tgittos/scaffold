# Prompt Optimizer Memory

## Key Patterns Discovered

### Dynamic Context Injection Conflicts
- System prompts can be undermined by runtime-injected context that appears later in the context window (recency bias)
- Always check the FULL prompt pipeline, not just the system prompt file
- In scaffold: `context_enhancement.c` injects todo instructions that can contradict system prompt directives
- See: `agent-prompt-patterns.md` for details

### Agent Prompt Anti-Patterns
- Hardcoded language assumptions (e.g., `python3 -m pytest`) make agents brittle in polyglot environments
- "Read, Edit, Test" loops without hypothesis formation lead to premature editing
- Vague escalation criteria ("complex projects") produce agents that never escalate
- Tool lists without usage strategy produce agents that use tools randomly
- "Act immediately" without error recovery strategy produces agents that retry the same failed approach

### Effective Agent Prompt Structures
1. Identity + critical rules (front-loaded, 3-5 lines)
2. Bootstrapping protocol (for agents entering unknown environments)
3. Task complexity classifier with concrete decision criteria
4. Core loop with hypothesis step before editing
5. Error recovery with escalation triggers (e.g., "after 3 failed attempts, reassess")
6. Tool strategy (when to use each, not just what they do)
7. Memory triggers (concrete events that warrant storing/recalling)

### Token Efficiency
- Agent prompts that are too SHORT often underperform — missing critical procedures
- Target ~85% load-bearing tokens (vs typical ~45%)
- Mode prompts should be <100 tokens each — behavioral constraints only
- Remove: implementation details, meta-instructions, reference material that belongs elsewhere

## File References
- `agent-prompt-patterns.md` - Detailed patterns for autonomous coding agents
