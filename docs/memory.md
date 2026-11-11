# Memory System

Scaffold has a persistent long-term memory powered by a vector database. It can store, search, and retrieve information across sessions using semantic similarity.

## How it works

When scaffold remembers something, it:

1. Generates a vector embedding of the content (via OpenAI's embedding API or a local embedding service)
2. Stores the embedding in an HNSWLIB vector index for fast similarity search
3. Stores the content and metadata as JSON files
4. Persists everything to `~/.local/scaffold/`

When scaffold needs to recall information, it embeds the query and finds the most semantically similar memories.

## Scopes

Memories and conversation history are scoped to the project scaffold was started in. The project is the nearest directory at or above the working directory that contains a `.git` entry (a repository or a git worktree), or the working directory itself when there is none. Subdirectories of a repository share its scope; each worktree has its own.

| Scope | Index | Holds |
|-------|-------|-------|
| `project` | `memory_<dir>_<hash>` | Facts about the current project |
| `user` | `memory_user` | Preferences that apply in every project |
| history | `conversations_<dir>_<hash>` | Recent conversation turns, recapped at startup |

`<dir>` is the project directory's name and `<hash>` is a hash of its absolute path.

## Memory tools

The LLM has three memory tools available:

### `remember`

Stores information in long-term memory.

```
"Remember that this project uses PostgreSQL 15 and all queries should use parameterized statements."
```

Parameters:
- `content` (required) -- The information to store
- `type` (optional) -- Category: `user_preference`, `fact`, `instruction`, `correction`
- `source` (optional) -- Origin: `conversation`, `web`, `file`
- `importance` (optional) -- Level: `low`, `normal`, `high`, `critical`
- `scope` (optional, default `project`) -- `project` or `user`

### `recall_memories`

Searches for relevant memories using semantic similarity.

```
"What database does this project use?"
```

Parameters:
- `query` (required) -- What to search for
- `k` (optional, default 5) -- Number of results to return

Searches the project and user scopes together and returns memories ranked by similarity score (0-1), each with its `scope` and metadata.

### `forget_memory`

Deletes a specific memory by ID. IDs are per scope, so pass the `scope` that `recall_memories` reported (default `project`).

## Interactive memory management

Use the `/memory` slash command for direct memory management:

```bash
# List memories in the current project's index
/memory list

# Search for memories about databases
/memory search database configuration

# View full details of a memory
/memory show 42

# Edit a memory's metadata
/memory edit 42 importance critical

# Edit a memory's content (auto re-embeds)
/memory edit 42 content "Project uses PostgreSQL 16 (upgraded from 15)"

# View all vector indices
/memory indices

# View statistics for an index
/memory stats memory_user
```

## Context enhancement

Scaffold automatically enhances each prompt with relevant memories. Before sending your message to the LLM, it:

1. Embeds your message
2. Searches the project and user memory scopes for relevant entries
3. Includes the most relevant memories as context

This means scaffold remembers things across sessions without you needing to repeat yourself.

## Storage locations

| Data | Path | Format |
|------|------|--------|
| Vector indices | `~/.local/scaffold/*.index` | HNSWLIB binary |
| Index metadata | `~/.local/scaffold/*.meta` | JSON |
| Documents | `~/.local/scaffold/documents/{index}/doc_{id}.json` | JSON |
| Chunk metadata | `~/.local/scaffold/metadata/{index}/chunk_{id}.json` | JSON |

## Embedding providers

Scaffold supports two embedding providers:

### OpenAI (default)

Uses `text-embedding-3-small` (1536 dimensions) by default. The actual dimension is resolved by probing the configured model at startup, so any model dimension works; if the probe fails, embedding-based search is disabled for the session. Configure with:

```json
{
  "embedding_api_url": "https://api.openai.com/v1/embeddings",
  "embedding_model": "text-embedding-3-small"
}
```

### Local embeddings

Point at a local embedding server (LM Studio, Ollama, omlx). No API key is needed:

```json
{
  "embedding_api_url": "http://localhost:1234/v1/embeddings",
  "embedding_model": "all-MiniLM-L6-v2"
}
```

Supported local models include Qwen3-Embedding, all-MiniLM, all-mpnet, and others. The vector dimension is resolved from the model via a startup probe (no per-model configuration needed).
