# Diff Reviewer Memory

## Scaffold JSON Output Format
- `--json` mode emits JSONL on stdout. Each line is a JSON object with a `type` field.
- Types: `assistant` (with `message.content` array of `text`/`tool_use`/`tool_result` blocks), `user`, `system`, `result`.
- `json_output_result()` exists in `lib/ui/json_output.c` but is **never called in production code** (only defined + tested). So `type: "result"` lines are never emitted.
- The actual assistant text is in `type: "assistant"` messages with `message.content[].type == "text"`.

## Scaffold CLI Flags
- `--system-prompt-file <path>`: Reads file, sets `worker_system_prompt`, then `unlink()`s the file.
- `--json`: Enables JSONL output on stdout.
- `--yolo`: Disables approval gates.
- `--home <path>`: Overrides `~/.local/scaffold`.
- `--model <name>`: Model override.
- `--no-stream`: Disables streaming (not required for subprocess capture but could simplify output).

## Evals Structure
- Python package under `evals/` managed by `uv`.
- Three benchmarks: swebench, feabench, contextbench.
- Common pattern: config loading, scaffold binary invocation, JSONL parsing, git patch extraction.

## Common Patterns to Watch
- argparse defaults vs config file fallbacks: `args.x or config.x` doesn't work when argparse has a non-falsy default.
- Temp file cleanup: scaffold `unlink()`s the system prompt file, but if scaffold fails to start, the temp file leaks.
- Output file append mode: re-runs accumulate stale data.
