# Plugins

Scaffold supports a subprocess-based plugin system. Plugins are standalone executables that communicate with scaffold over stdin/stdout using JSON-RPC 2.0. They can hook into the agent pipeline and provide custom tools.

Plugins are standalone executables that speak the protocol. Since scaffold is a zero-dependency binary, plugins should be written in C and compiled with [Cosmopolitan](https://github.com/jart/cosmopolitan) to produce cross-platform binaries that work everywhere scaffold does.

## Installation

The primary way to install plugins is by downloading pre-built binaries from GitHub Releases:

```bash
# Download a plugin from its GitHub Release
curl -L -o ~/.local/scaffold/plugins/session-transcript \
  https://github.com/<owner>/ralph/releases/download/plugin-session-transcript-v1.0.0/session-transcript
chmod +x ~/.local/scaffold/plugins/session-transcript
```

Plugins are also bundled as convenience snapshots in scaffold releases. To keep plugins up to date automatically, use `scaffold --update-plugins`.

Alternatively, drop any executable into `~/.local/scaffold/plugins/`:

```bash
cp my-plugin ~/.local/scaffold/plugins/
chmod +x ~/.local/scaffold/plugins/my-plugin
```

Scaffold discovers plugins on startup. Restart scaffold after adding or removing plugins. Use `/plugins` in interactive mode to see what's loaded.

## Distribution & Updates

### Plugin versioning

Each plugin is versioned independently using namespaced git tags in the format `plugin-<name>-v*` (e.g., `plugin-session-transcript-v1.2.0`). The build system compiles the version from the git tag into the plugin binary, making it available in the plugin's manifest during the initialize handshake.

### GitHub Releases

Each plugin version gets its own GitHub Release, tagged with the namespaced tag. Release assets include cross-platform Cosmopolitan binaries ready for installation. Scaffold releases also include plugin binaries as convenience snapshots so users get working plugins out of the box.

### Update checking

Scaffold checks for plugin updates at startup by querying GitHub Releases for each installed plugin. Users can also check and update manually:

- `scaffold --check-plugin-updates` — Query GitHub Releases for newer versions of all installed plugins and report available updates.
- `scaffold --update-plugins` — Download and install the latest versions of all plugins from GitHub Releases, replacing the existing binaries in `~/.local/scaffold/plugins/`.

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

Plugin names must not contain underscores, forward slashes, or backslashes, and must be 1-64 characters. This is enforced during the handshake -- plugins with invalid names are rejected. The first `_` after the `plugin_` prefix is used as the delimiter for tool routing.

## Limits

- Maximum 16 plugins
- 5-second timeout per hook response (plugin is skipped on timeout)
- Plugins that fail the handshake are killed and skipped

## Error handling

- If a plugin crashes mid-session, its hook calls fail silently and scaffold continues
- If a plugin times out on a hook, it is skipped for that event
- If a plugin fails the initialization handshake, it is killed and excluded
- An empty or missing `~/.local/scaffold/plugins/` directory is fine -- scaffold runs normally

## Writing plugins

Scaffold is a single binary with no runtime dependencies. Plugins must match this philosophy: **write plugins in C and compile with Cosmopolitan**. This produces cross-platform binaries that work everywhere scaffold does — no interpreters, no runtimes, no dependencies for your users to install.

Use cJSON (vendored in `deps/cJSON-*`) for JSON parsing. See `plugins/session-transcript/` for a complete reference implementation.

## Examples

### Session transcript

Records all agent conversations to markdown files. Source is in `plugins/session-transcript/`.

Subscribes to `post_user_input`, `post_llm_response`, and `post_tool_execute` hooks to capture user messages, model responses, and tool calls. Large tool arguments (like `apply_patch`) are automatically summarized to keep transcripts readable.

Configuration via `~/.local/scaffold/transcript.conf`:

```
output_dir=~/my-transcripts
max_arg_length=500
max_result_length=2000
```

Build and install (version is compiled in from git tags via `plugin-session-transcript-v*`):

```bash
make plugins
cp out/plugins/session-transcript ~/.local/scaffold/plugins/
```

Transcripts are written to `~/.local/scaffold/transcripts/` (one markdown file per session).

### Plugin template

Minimal boilerplate for a Cosmopolitan C plugin:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>

static void send_json(cJSON *root) {
    char *str = cJSON_PrintUnformatted(root);
    if (str) { fprintf(stdout, "%s\n", str); fflush(stdout); free(str); }
    cJSON_Delete(root);
}

static cJSON *make_response(int id) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);
    return root;
}

int main(void) {
    char line[1024 * 1024];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        cJSON *req = cJSON_Parse(line);
        if (!req) continue;

        const char *method = "";
        cJSON *m = cJSON_GetObjectItem(req, "method");
        if (m && cJSON_IsString(m)) method = m->valuestring;

        int id = 0;
        cJSON *id_j = cJSON_GetObjectItem(req, "id");
        if (id_j && cJSON_IsNumber(id_j)) id = id_j->valueint;

        if (strcmp(method, "initialize") == 0) {
            cJSON *root = make_response(id);
            cJSON *result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "name", "my-plugin");
            cJSON_AddStringToObject(result, "version", "1.0.0");
            cJSON_AddStringToObject(result, "description", "What this plugin does");
            cJSON *hooks = cJSON_CreateArray();
            /* Add hooks: "post_user_input", "post_llm_response", etc. */
            cJSON_AddItemToObject(result, "hooks", hooks);
            cJSON *tools = cJSON_CreateArray();
            cJSON_AddItemToObject(result, "tools", tools);
            cJSON_AddNumberToObject(result, "priority", 500);
            cJSON_AddItemToObject(root, "result", result);
            send_json(root);
        } else if (strcmp(method, "shutdown") == 0) {
            cJSON *root = make_response(id);
            cJSON *result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "ok", 1);
            cJSON_AddItemToObject(root, "result", result);
            send_json(root);
            cJSON_Delete(req);
            break;
        } else {
            /* Handle hooks/tools here */
            cJSON *root = make_response(id);
            cJSON *result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "action", "continue");
            cJSON_AddItemToObject(root, "result", result);
            send_json(root);
        }

        cJSON_Delete(req);
    }
    return 0;
}
```

Build with:

```bash
cosmocc -O2 -std=c11 -I deps/cJSON-1.7.18 -o my-plugin main.c deps/cJSON-1.7.18/cJSON.c
cp my-plugin ~/.local/scaffold/plugins/
```
