# Bug Report

Track of issues observed while running scaffold. One section per bug:
symptom, where it shows up, and what was expected. Newest entries first;
entry numbers are discovery order, not priority — each entry carries its
own **Priority** line instead.

## 4. Shell-tool cancellation edge cases

**Priority: low** — none affects the common Ctrl-C or timeout path.

1. *Narrow kill fallback.* `_kill_tree()` in
   `src/tools/python_defaults/shell.py` catches only `ProcessLookupError` and
   `PermissionError` from `os.killpg()`. Any other `OSError` (a platform where
   `setsid`/`killpg` misbehave under Cosmopolitan) escapes to the outer
   `except`: the process is not killed and captured stdout is replaced with
   `""`. Catch `OSError` and fall back to `proc.kill()`.
2. *Orphaned reader threads.* When `t_out`/`t_err.join(timeout=2)` expires —
   a background process that called `setsid` still holds the pipe — the
   reader thread keeps running, keeps calling `_ralph_sys.progress()` into
   whatever tool's spinner runs next, and its later output is dropped.
   Close the pipe or signal the reader to stop after a join timeout.
3. *Interrupt flag leaks between tests.* In
   `test/tools/test_python_integration.c`, a failed assertion between
   `interrupt_handler_trigger()` and `interrupt_clear()` longjmps past the
   clear, leaving the flag latched for every later test. Clear it in
   `tearDown()`.
4. *Slow, collision-prone sentinel tests.* The group-kill tests use fixed
   `/tmp/sf_cancel` / `/tmp/sf_timeout` paths and two `sleep(6)` waits
   (~12s of suite time). Use `mkstemp`-style paths and a ~2s fixture sleep.
5. *No controlling terminal.* `start_new_session` detaches the command from
   the terminal, so prompts that open `/dev/tty` (ssh, sudo, git credential
   helpers) now fail immediately with `ENXIO` instead of hanging until the
   timeout. Intended, but undocumented in the `shell` tool description.

## 3. Input handling is weak: no graceful interrupt, no input queue, no usable history

**Priority: high** — the only entry that blocks day-to-day interactive use.
The four gaps below are four separate bugs; the **Fix plan** section
decomposes it into five independently shippable steps.

**Symptom:** In interactive mode the line editor is a fire-and-forget slot.
Once you submit a message the process owns the terminal, and the usual
readline conveniences are missing or broken. Four concrete gaps:

- **No graceful interrupt.** Ctrl-C while a turn is running does not cleanly
  stop it.
  - *During the HTTP stream* a Ctrl-C does abort the request: the
    `XFERINFOFUNCTION` in `lib/network/http_client.c` returns 1, and the SSE
    callback in `lib/agent/streaming_handler.c` drops data. History stays
    clean (the user message is only appended *after* the stream completes, so
    an aborted request commits nothing). But the abort is barely visible:
    `streaming_process_message()` just prints "Request cancelled." and
    returns — indistinguishable in spirit from an error, and the turn simply
    ends.
  - *During tool execution* the interrupt is only checked between phases
    (`tool_batch_execute` phases 1 and 3 in `lib/agent/tool_batch_executor.c`),
    never inside a tool call. The Python-backed tools (`shell`, `python` from
    `~/.local/scaffold/tools/`) run in the embedded interpreter: `shell`
    blocks in `subprocess.run(..., capture_output=True)`, and the C-side
    interrupt flag is not visible to Python, so a long-running command
    ignores Ctrl-C — the only escape is the tool's own `timeout` (default
    30s, capped at 300s).
  - The interrupt is a single latched flag (`lib/util/interrupt.c`), and the
    REPL clears it unconditionally at the top of each loop iteration
    (`repl_run_session`, `lib/agent/repl.c`). There is no "first press aborts,
    second press does X" distinction, and no "stop the whole turn" path.

- **Ctrl-C to clear the buffer doesn't work.** `rl_initialize()` is *never
  called* anywhere in the codebase (verified: zero call sites), so readline
  never installs its own SIGINT handling — the app's bare flag-setting
  handler (`lib/util/interrupt.c`, installed in `repl_run_session`) is always
  the live disposition. Ctrl-C therefore never kills the in-progress input
  line the way it does in a shell.

- **No message queue.** `repl_line_callback` calls `session_process_message()`
  synchronously (`lib/agent/repl.c`), which blocks the `select()` loop for
  the entire turn (LLM round-trip + every tool call). While a turn runs there
  is no prompt to type into and no queue to stage the next message for
  afterwards. (`lib/agent/async_executor.c` exists with exactly this shape —
  worker thread plus a pipe notifier the `select()` loop would add — but is
  never referenced from the interactive path; it is dead code.)

- **Readline history not usable.** `using_history()` / `add_history()` are
  called in `lib/agent/agent.c`, so history exists in-session, but:
  - `write_history()` / `read_history()` are *never* called (verified), so
    history does not survive a restart.
  - The line editor is torn down and re-installed (`rl_callback_handler_remove`
    + `rl_callback_handler_install`) after every turn and every notification,
    and the spinner runs on a dedicated thread that `fputs` frames straight to
    stdout while a tool is executing (`lib/ui/spinner.c`) — all while the
    readline handler is installed. (Inference from the code: this teardown/
    contention is the likely reason up-arrow recall misbehaves; the exact
    mechanism is not yet confirmed at runtime.)
  - The custom SIGINT handler (above) also suppresses readline's line-kill,
    so the buffer half of the interrupt contract is doubly broken.

**Where:** `lib/agent/repl.c` (`repl_run_session`, `repl_line_callback`,
`repl_install_prompt`), `lib/agent/agent.c` (interactive path:
`using_history()` → `repl_run_session`), `lib/util/interrupt.c`,
`lib/agent/streaming_handler.c`, `lib/agent/tool_batch_executor.c`,
`lib/agent/iterative_loop.c`, `lib/network/http_client.c`,
`lib/agent/async_executor.c`, `lib/ui/spinner.c`,
`~/.local/scaffold/tools/shell.py` (embedded Python tool).

**Expected:** Standard editor behaviour:
1. Ctrl-C while a turn is running = graceful abort of the *entire* turn —
   stop the stream, cancel pending/running tools (including killing the
   child process a `shell`/`python` tool is waiting on), do not commit a
   partial response to history — returning to a clean prompt.
2. Ctrl-C on an idle/empty prompt = clear the current input buffer (restore
   readline's line-kill on Ctrl-C).
3. Messages typed while a turn is running are queued and processed in order
   once the current turn finishes (or explicitly rejected with feedback).
4. Up/down arrow navigate prior submissions reliably, and history persists
   across restarts (`write_history`/`read_history` at exit/start).

**Notes:** Root causes, three in one:
1. *Synchronous submission inside the callback.* The `select()` loop cannot
   service the editor while a turn is in flight, so queueing and editor
   behaviour both degrade. `async_executor` was built to fix exactly this
   and is unused — wiring it in is the smallest structural fix.
2. *Readline signal handling never initialised.* `rl_initialize()` is never
   called. The fix is: set `rl_catch_signals = 0` *before* `rl_initialize()`,
   then keep the app's handler (`lib/util/interrupt.c`) as the single
   press-sensitive brain — turn running → abort turn; idle prompt →
   `rl_kill_line()` + `rl_redraw()`. The `rl_catch_signals = 0` is the
   load-bearing detail: with the default (`1`), `rl_initialize()` installs
   readline's *own* SIGINT disposition and the two handlers fight —
   readline line-kills unconditionally, even mid-turn, which breaks the
   expected behaviour (mid-turn Ctrl-C must abort the turn, not just clear
   the line).
3. *No tool-level interrupt.* The embedded Python tools can't see the C
   interrupt flag at all — `shell` needs a cancellation channel (an fd or
   file the Python side polls while waiting, or the C side raising
   `KeyboardInterrupt` in the interpreter thread), and `subprocess.run`
   replaced with `Popen` + poll-and-kill. One trap applies to the *existing*
   `timeout` path too: `shell.py` runs `subprocess.run(command, shell=True,
   ...)`, and on timeout CPython kills only the direct child (`sh`) — so
   `grep foo | less` already leaks its grandchild processes today. The
   cancellation (and timeout) path needs `start_new_session=True` +
   `os.killpg()` on the process group, or both paths silently fail on any
   piped command. Until this lands, "graceful abort" stops at the tool
   boundary rather than inside the tool.

**Fix plan.** Five steps, in order. Each is independently shippable and
verifiable — land them as separate commits, not one big change:

1. Wire `async_executor` into the interactive path (replaces the
   synchronous `session_process_message()` in `repl_line_callback`; the
   `select()` loop adds the notify fd, and `repl_line_callback` pushes onto
   a queue instead of blocking).
   *Done when:* a message typed while a turn is running is queued, the
   prompt stays live to accept it, and it is processed in order after the
   turn finishes.
2. `rl_catch_signals = 0` + `rl_initialize()` + press-sensitive app handler
   (per Notes item 2 above).
   *Done when:* Ctrl-C on a half-typed line clears the buffer and drops a
   fresh prompt; mid-turn Ctrl-C aborts the turn without clearing the line.
3. `read_history()` at start / `write_history()` at exit, with a stable
   history file (e.g. `~/.local/share/scaffold/history`).
   *Done when:* up-arrow recalls a message from the previous run.
4. Cancellation channel in `shell`/`python` + `Popen` + process-group kill
   (per Notes item 3 above).
   *Done when:* Ctrl-C while a 60-second `shell` command is running returns
   to a clean prompt within ~1 second, with the child *process tree* dead
   (verify with a piped command like `sleep 60 | cat`).
   *Landed 2026-11-20:* `_ralph_sys.cancelled()` + `start_new_session`/`killpg`
   in `shell` (the `python` tool stays timeout-only). All five steps of this
   entry are complete.
5. Distinct, visible "cancelled" status for the stream-abort path (replace
   the bare "Request cancelled." that currently reads like an error).
   *Done when:* an aborted turn prints a cancellation notice, commits
   nothing to history, and returns to the prompt.

## 2. Inconsistent vertical spacing in output

**Priority: cosmetic** — fix as part of bug #1's renderer, not before it.

**Symptom:** Blank-line spacing between parts of the rendered output is
irregular — some gaps show one blank line, others two (or more), with no
consistent rule. It is most visible between the reasoning/thinking block and
the answer, around tool executions, and between paragraphs of a streamed
response.

**Where:** Interactive mode. `display_streaming_text()` in
`lib/ui/output_formatter.c` writes chunks verbatim (`fwrite`), so the exact
whitespace the model emits is what appears. On top of that, several paths
each add their own newlines: the thinking→answer boundary has a special-cased
blank-line top-up, `display_streaming_complete()` appends a leading newline,
and tool-result logging / message notifications add their own blank lines.
There is no normalisation step, so the combined effect is that spacing
depends on which sequence of events happened.

**Expected:** Consistent spacing: normalise whitespace in the display layer,
 e.g. collapse 3+ newlines to exactly two (one blank line), exactly one blank
 line between logical blocks (thinking / answer / tool output), and no stray
 double newlines mid-paragraph. That gives uniform output regardless of what
 the model emitted or which UI events interleaved.

**Notes:** Implement the normalisation *inside* the markdown renderer (bug
#1), not as a separate post-processing layer. A line-buffered renderer is
already a state machine that decides what each emitted line looks like, so it
can canonicalise paragraph and list spacing as a side effect — and this is
the only safe place to do it: a standalone normaliser cannot wait for a
"third newline" without holding back every paragraph boundary, and once
newlines are flushed to a live streaming terminal they cannot be taken back.
One component, one rule: exactly one blank line between logical blocks
(thinking / answer / tool output), never 3+ raw newlines mid-paragraph.

## 1. Markdown not rendered in the terminal

**Priority: cosmetic** — land as the same renderer component as #2.

**Symptom:** Assistant output is printed verbatim. Markdown syntax (headings,
bold, tables, lists, code fences) appears in the terminal as raw text —
`**bold**`, `| col |` pipes, `###` hashes — rather than being rendered.

**Where:** Interactive mode, streaming text output. The output formatter
(`lib/ui/output_formatter.c`) only applies terminal color/escape sequences;
there is no markdown parsing or rendering stage.

**Expected:** Assistant responses that use markdown should be rendered in the
terminal — e.g. bold for emphasis, tables drawn with box-drawing characters,
code blocks indented or fenced, headings formatted as stand-out lines.

**Notes:** Cosmetic, but it degrades long responses (status summaries,
tables) significantly. A minimal `stilo`/`glow`-style renderer would cover
the common constructs; at minimum, strip or transform the `**`, `##`, and
table pipe characters so output reads cleanly. This is also the
implementation home for bug #2's spacing normalisation — one renderer,
both bugs.

---

_Add new bugs above this line. Keep each entry self-contained: a
**Priority** line, symptom, where it reproduces, what was expected._
