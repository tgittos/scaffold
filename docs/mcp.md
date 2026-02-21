# MCP Servers

Scaffold supports the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) for connecting external tool servers. This lets you extend scaffold with tools from any MCP-compatible server.

## Configuration

Add MCP servers to your `scaffold.config.json`:

```json
{
  "mcpServers": {
    "my-server": {
      "command": "/path/to/server",
      "args": ["--flag", "value"],
      "env": {
        "API_KEY": "${MY_API_KEY}"
      }
    }
  }
}
```

### Server types

#### STDIO (local process)

The most common type. Scaffold spawns the server as a child process and communicates over stdin/stdout:

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path/to/directory"]
    }
  }
}
```

#### HTTP (remote server)

Connect to a remote MCP server over HTTP:

```json
{
  "mcpServers": {
    "remote-tools": {
      "url": "https://mcp.example.com/api"
    }
  }
}
```

#### SSE (Server-Sent Events)

Connect to a server using Server-Sent Events for streaming:

```json
{
  "mcpServers": {
    "streaming-server": {
      "url": "https://mcp.example.com/sse"
    }
  }
}
```

## Environment variables

MCP server configurations support environment variable expansion:

```json
{
  "env": {
    "API_KEY": "${MY_API_KEY}",
    "ENDPOINT": "${MCP_ENDPOINT:-https://default.example.com}"
  }
}
```

Syntax:
- `${VAR}` -- Expands to the value of `VAR`
- `${VAR:-default}` -- Expands to `VAR` if set, otherwise uses `default`

## Tool namespacing

MCP tools are registered with a namespace to avoid conflicts with built-in tools:

```
mcp_{servername}_{toolname}
```

For example, a tool named `search` from a server named `docs` becomes `mcp_docs_search`.

## How it works

1. On startup, scaffold connects to all configured MCP servers
2. It discovers available tools via the JSON-RPC `tools/list` method
3. Tools are registered in scaffold's tool registry with their namespaced names
4. When the LLM calls an MCP tool, scaffold routes the call to the appropriate server via JSON-RPC `tools/call`

## Approval gates

MCP tools are in the `mcp` category, which is **gated by default**. You'll be prompted before any MCP tool executes. To auto-allow:

```json
{
  "approval_gates": {
    "categories": {
      "mcp": "allow"
    }
  }
}
```

Or per-session:

```bash
./scaffold --allow-category=mcp
```

## Graceful degradation

MCP is optional. If no servers are configured, or if a server fails to connect, scaffold continues operating with its built-in tools. Server connection failures are logged but don't block startup.
