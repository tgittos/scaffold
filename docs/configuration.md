# Configuration

Scaffold uses a layered configuration system. Settings are resolved in priority order:

1. CLI flags (highest priority)
2. Local config file (`scaffold.config.json` in the current directory)
3. User config file (`~/.local/scaffold/config.json`)
4. Environment variables
5. Built-in defaults (lowest priority)

## Config file

Create a `scaffold.config.json` in your project directory or `~/.local/scaffold/config.json` for global settings:

```json
{
  "api_url": "https://api.openai.com/v1/chat/completions",
  "model": "gpt-4o",
  "openai_api_key": "sk-...",
  "context_window": 128000,
  "enable_streaming": true,
  "check_updates": true
}
```

### All config keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `api_url` | string | `https://api.openai.com/v1/chat/completions` | LLM API endpoint |
| `model` | string | `gpt-5-mini-2025-08-07` | Default model |
| `openai_api_key` | string | | OpenAI API key |
| `anthropic_api_key` | string | | Anthropic API key |
| `openai_api_url` | string | | Custom OpenAI-compatible endpoint |
| `embedding_api_url` | string | | Embedding service endpoint (defaults to the OpenAI embeddings API) |
| `embedding_model` | string | | Model for embeddings |
| `system_prompt` | string | | Custom system prompt override |
| `context_window` | number | `8192` | Context window size in tokens |
| `tool_result_max_bytes` | number | `32768` | Cap on each tool result sent to the model; longer results keep head and tail around an omission marker |
| `max_tokens` | number | `-1` | Max output tokens (`-1` = unlimited) |
| `enable_streaming` | boolean | `true` | Enable real-time response streaming |
| `check_updates` | boolean | `true` | Check for updates on startup |
| `shell_max_timeout` | number | `300` | Upper bound in seconds on the `shell` tool's `timeout` |

#### API retry settings

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `api_max_retries` | number | `3` | Number of retry attempts on failure |
| `api_retry_delay_ms` | number | `1000` | Initial retry delay in milliseconds |
| `api_backoff_factor` | number | `2.0` | Exponential backoff multiplier |

#### Orchestration settings

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `max_subagents` | number | `5` | Maximum concurrent subagents |
| `subagent_timeout` | number | `300` | Subagent timeout in seconds |
| `max_workers_per_goal` | number | `3` | Max workers per GOAP goal (1-20) |

#### Model tiers

Scaffold supports model tiers that map logical names to specific models. Configure them in the `models` section:

```json
{
  "models": {
    "simple": "o4-mini",
    "standard": "gpt-5-mini-2025-08-07",
    "high": "gpt-5.2-2025-12-11"
  }
}
```

| Tier | Default | Use case |
|------|---------|----------|
| `simple` | `o4-mini` | Fast, lightweight tasks |
| `standard` | `gpt-5-mini-2025-08-07` | Balanced capability/speed |
| `high` | `gpt-5.2-2025-12-11` | Most capable, complex reasoning |

## Environment variables

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI API key (overrides config file) |
| `ANTHROPIC_API_KEY` | Anthropic API key (overrides config file) |
| `EMBEDDING_API_URL` | Embedding service endpoint (overrides config file) |
| `EMBEDDING_MODEL` | Embedding model (overrides config file) |

Environment variables take precedence over config file values.

## Provider detection

Scaffold auto-detects your LLM provider from the API URL:

| URL pattern | Provider |
|-------------|----------|
| `api.anthropic.com` | Anthropic |
| `api.openai.com` | OpenAI |
| `chatgpt.com/backend-api/codex` | Codex (via `--login` OAuth) |
| `localhost:*` | Local AI (LM Studio, Ollama, etc.) |

### Using local models

Point scaffold at a local OpenAI-compatible server:

```json
{
  "api_url": "http://localhost:1234/v1/chat/completions",
  "model": "local-model-name"
}
```

This works with LM Studio, Ollama (with OpenAI compatibility), and any server implementing the OpenAI chat completions API.

### Using Anthropic

```json
{
  "api_url": "https://api.anthropic.com/v1/messages",
  "model": "claude-sonnet-4-20250514",
  "anthropic_api_key": "sk-ant-..."
}
```

## Tool execution

Scaffold runs every tool call as the LLM issues it. There is no approval prompt, allowlist or category configuration: file writes, shell commands, network requests and MCP, Python and plugin tools all execute directly. An `approval_gates` section left in an older config file is ignored.

## Project-specific prompts (AGENTS.md)

Scaffold reads an `AGENTS.md` file from the current directory and appends it to the system prompt. Use this to give scaffold project-specific context:

```markdown
# Project: My App

## Stack
- Frontend: React with TypeScript
- Backend: Express with PostgreSQL
- Testing: Jest

## Conventions
- Use functional components with hooks
- API responses follow JSON:API format
- All endpoints need input validation
```

### File references

Reference other files in your `AGENTS.md` with `@filename` syntax:

```markdown
See @ARCHITECTURE.md for system design.
Review @API_SPEC.md for endpoint definitions.
```

Referenced files are expanded inline and wrapped in XML tags. Each file is included only once (deduplicated).
