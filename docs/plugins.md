# Plugins

Scaffold supports a subprocess-based plugin system. Plugins are standalone executables that communicate with scaffold over stdin/stdout using JSON-RPC 2.0. They can hook into the agent pipeline and provide custom tools.

Plugins can be written in any language -- Python, Go, Rust, C, shell scripts -- as long as they speak the protocol.

## Installation

Drop an executable into `~/.local/scaffold/plugins/`:

```bash
cp my-plugin ~/.local/scaffold/plugins/
chmod +x ~/.local/scaffold/plugins/my-plugin
```

Scaffold discovers plugins on startup. Restart scaffold after adding or removing plugins. Use `/plugins` in interactive mode to see what's loaded.

## How it works

1. Scaffold scans `~/.local/scaffold/plugins/` for executable files (hidden files are skipped)
2. Each plugin is spawned as a child process with stdin/stdout pipes
3. Scaffold sends an `initialize` request; the plugin responds with its manifest (name, hooks, tools, priority)
4. Plugin-provided tools are registered in the tool registry as `plugin_<name>_<tool>`
5. During the agent loop, scaffold dispatches hook events to subscribed plugins
6. On shutdown, scaffold sends a `shutdown` message, then SIGTERM, then SIGKILL if needed

## Protocol

All messages are JSON-RPC 2.0, newline-terminated, over stdin/stdout. One JSON object per line.

### Initialize handshake

Scaffold sends:

```json
{"jsonrpc":"2.0","method":"initialize","params":{"protocol_version":1},"id":1}
```

Plugin responds with its manifest:

```json
{"jsonrpc":"2.0","id":1,"result":{
  "name":"my-plugin",
  "version":"1.0.0",
  "description":"What this plugin does",
  "hooks":["context_enhance","post_user_input"],
  "tools":[
    {
      "name":"my_tool",
      "description":"What the tool does",
      "parameters":[
        {"name":"query","type":"string","description":"Search query","required":true},
        {"name":"limit","type":"number","description":"Max results","required":false}
      ]
    }
  ],
  "priority":500
}}
```

Manifest fields:

| Field | Required | Default | Description |
|-------|----------|---------|-------------|
| `name` | yes | -- | Plugin identifier. Must not contain underscores. |
| `version` | no | `"0.0.0"` | Semantic version string |
| `description` | no | `""` | Human-readable description |
| `hooks` | no | `[]` | Hook names this plugin subscribes to |
| `tools` | no | `[]` | Tools this plugin provides |
| `priority` | no | `500` | Execution order (lower runs first) |

### Hook events

Scaffold sends a hook event as a JSON-RPC request. The plugin responds with an action and any modified data.

```json
{"jsonrpc":"2.0","method":"hook/post_user_input","params":{"message":"user text"},"id":2}
```

```json
{"jsonrpc":"2.0","id":2,"result":{"action":"continue","message":"modified text"}}
```

Actions:

| Action | Meaning |
|--------|---------|
| `continue` | Pass to the next plugin, then proceed normally |
| `stop` | Halt the hook chain. For `pre_tool_execute`, the plugin provides the tool result. |
| `skip` | Discard the event entirely (`post_user_input` only) |

### Tool execution

When the LLM calls a plugin tool, scaffold sends:

```json
{"jsonrpc":"2.0","method":"tool/execute","params":{"name":"my_tool","arguments":{"query":"test"}},"id":3}
```

Plugin responds:

```json
{"jsonrpc":"2.0","id":3,"result":{"success":true,"result":"tool output here"}}
```

On error:

```json
{"jsonrpc":"2.0","id":3,"result":{"success":false,"result":"error message"}}
```

### Shutdown

```json
{"jsonrpc":"2.0","method":"shutdown","params":{},"id":4}
```

```json
{"jsonrpc":"2.0","id":4,"result":{"ok":true}}
```

The plugin should exit after responding.

## Hooks

Hooks let plugins intercept and transform data at specific points in the agent pipeline.

### post_user_input

Fires after the user types a message, before processing.

Request params: `{"message":"user input"}`

Response fields: `{"action":"...","message":"transformed input"}`

Use cases: input filtering, logging, command interception, text transformation.

### context_enhance

Fires during prompt building. Lets plugins inject dynamic context into the system prompt.

Request params: `{"user_message":"...","dynamic_context":"current context"}`

Response fields: `{"dynamic_context":"current context\n\n# Extra Context\n..."}`

Use cases: injecting git status, project metadata, environment info, custom instructions.

Stop/skip actions are ignored for this hook -- context always accumulates.

### pre_llm_send

Fires after the full prompt is built, before sending to the LLM.

Request params: `{"base_prompt":"system prompt","dynamic_context":"dynamic context"}`

Response fields: `{"action":"...","base_prompt":"...","dynamic_context":"..."}`

Use cases: prompt modification, request logging, analytics.

### post_llm_response

Fires after the LLM responds, before display and tool execution.

Request params: `{"text":"response text","tool_calls":[{"name":"...","arguments":"..."}]}`

Response fields: `{"action":"...","text":"transformed text"}`

Use cases: response filtering, analytics, text transformation.

### pre_tool_execute

Fires before a tool call executes.

Request params: `{"tool_name":"write_file","arguments":"{...}"}`

Response fields: `{"action":"continue"}` or `{"action":"stop","result":"{\"error\":\"blocked\"}"}`

Use cases: audit logging, blocking dangerous operations, argument modification.

Returning `stop` prevents the tool from executing and provides the result directly.

### post_tool_execute

Fires after a tool call returns.

Request params: `{"tool_name":"write_file","arguments":"{...}","result":"...","success":true}`

Response fields: `{"action":"...","result":"modified result"}`

Use cases: result transformation, logging, side effects.

## Priority

When multiple plugins subscribe to the same hook, they execute in priority order. Lower numbers run first. Default is 500.

```
Priority 100: security-audit   (runs first)
Priority 500: git-context      (runs second)
Priority 900: analytics        (runs last)
```

## Tool namespacing

Plugin tools are registered as `plugin_<pluginname>_<toolname>`. For example, a plugin named `git` with a tool named `log` becomes `plugin_git_log`.

Plugin names must not contain underscores (the first `_` after the `plugin_` prefix is used as the delimiter).

## Limits

- Maximum 16 plugins
- 5-second timeout per hook response (plugin is skipped on timeout)
- Plugins that fail the handshake are killed and skipped

## Error handling

- If a plugin crashes mid-session, its hook calls fail silently and scaffold continues
- If a plugin times out on a hook, it is skipped for that event
- If a plugin fails the initialization handshake, it is killed and excluded
- An empty or missing `~/.local/scaffold/plugins/` directory is fine -- scaffold runs normally

## Examples

### Python: git context plugin

Injects the current git branch and changed files into the system prompt.

```python
#!/usr/bin/env python3
"""Adds git repo context to agent prompts."""
import json, sys, subprocess

def git(cmd):
    try:
        return subprocess.check_output(
            ["git"] + cmd, stderr=subprocess.DEVNULL, text=True
        ).strip()
    except Exception:
        return None

def handle(request):
    method = request["method"]
    rid = request.get("id")

    if method == "initialize":
        return {"jsonrpc":"2.0","id":rid,"result":{
            "name":"git-context","version":"1.0.0",
            "description":"Adds git repo context to prompts",
            "hooks":["context_enhance"],"tools":[],"priority":600
        }}

    if method == "hook/context_enhance":
        ctx = request["params"].get("dynamic_context", "") or ""
        branch = git(["branch","--show-current"])
        if branch:
            ctx += f"\n\n# Git Context\nBranch: {branch}\n"
            status = git(["diff","--stat","HEAD"])
            if status:
                ctx += f"Changes:\n{status}\n"
        return {"jsonrpc":"2.0","id":rid,"result":{"dynamic_context":ctx}}

    if method == "shutdown":
        return {"jsonrpc":"2.0","id":rid,"result":{"ok":True}}

    return {"jsonrpc":"2.0","id":rid,"error":{
        "code":-32601,"message":"Unknown method"
    }}

for line in sys.stdin:
    request = json.loads(line.strip())
    response = handle(request)
    sys.stdout.write(json.dumps(response) + "\n")
    sys.stdout.flush()
    if request["method"] == "shutdown":
        break
```

Install:

```bash
cp git-context ~/.local/scaffold/plugins/
chmod +x ~/.local/scaffold/plugins/git-context
```

### Python: audit logger

Logs all tool calls to a file.

```python
#!/usr/bin/env python3
"""Logs tool execution to ~/.local/scaffold/audit.log."""
import json, sys, os, datetime

LOG = os.path.expanduser("~/.local/scaffold/audit.log")

def handle(request):
    method = request["method"]
    rid = request.get("id")

    if method == "initialize":
        return {"jsonrpc":"2.0","id":rid,"result":{
            "name":"audit-logger","version":"1.0.0",
            "description":"Logs tool calls to audit.log",
            "hooks":["pre_tool_execute"],"tools":[],"priority":100
        }}

    if method == "hook/pre_tool_execute":
        params = request["params"]
        ts = datetime.datetime.now().isoformat()
        entry = f"{ts} TOOL {params['tool_name']} {params.get('arguments','')}\n"
        with open(LOG, "a") as f:
            f.write(entry)
        return {"jsonrpc":"2.0","id":rid,"result":{"action":"continue"}}

    if method == "shutdown":
        return {"jsonrpc":"2.0","id":rid,"result":{"ok":True}}

    return {"jsonrpc":"2.0","id":rid,"result":{"action":"continue"}}

for line in sys.stdin:
    request = json.loads(line.strip())
    response = handle(request)
    sys.stdout.write(json.dumps(response) + "\n")
    sys.stdout.flush()
    if request["method"] == "shutdown":
        break
```

### Shell: simple tool provider

A minimal plugin in bash that provides a `uptime` tool.

```bash
#!/bin/bash
# Plugin that provides a system uptime tool.

while IFS= read -r line; do
    method=$(echo "$line" | jq -r '.method')
    id=$(echo "$line" | jq -r '.id')

    case "$method" in
        initialize)
            echo '{"jsonrpc":"2.0","id":'"$id"',"result":{"name":"system-info","version":"1.0.0","description":"System info tools","hooks":[],"tools":[{"name":"uptime","description":"Get system uptime","parameters":[]}],"priority":500}}'
            ;;
        tool/execute)
            result=$(uptime -p 2>/dev/null || uptime)
            echo '{"jsonrpc":"2.0","id":'"$id"',"result":{"success":true,"result":"'"${result//\"/\\\"}"'"}}'
            ;;
        shutdown)
            echo '{"jsonrpc":"2.0","id":'"$id"',"result":{"ok":true}}'
            exit 0
            ;;
        *)
            echo '{"jsonrpc":"2.0","id":'"$id"',"error":{"code":-32601,"message":"Unknown method"}}'
            ;;
    esac
done
```

### Plugin template

Minimal boilerplate for a Python plugin:

```python
#!/usr/bin/env python3
import json, sys

MANIFEST = {
    "name": "my-plugin",
    "version": "0.1.0",
    "description": "",
    "hooks": [],
    "tools": [],
    "priority": 500,
}

def handle(request):
    method = request["method"]
    rid = request.get("id")

    if method == "initialize":
        return {"jsonrpc":"2.0","id":rid,"result":MANIFEST}

    if method == "shutdown":
        return {"jsonrpc":"2.0","id":rid,"result":{"ok":True}}

    # Handle your hooks/tools here

    return {"jsonrpc":"2.0","id":rid,"error":{
        "code":-32601,"message":"Unknown method"
    }}

for line in sys.stdin:
    request = json.loads(line.strip())
    response = handle(request)
    sys.stdout.write(json.dumps(response) + "\n")
    sys.stdout.flush()
    if request["method"] == "shutdown":
        break
```
