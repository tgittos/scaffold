# Memory System

Scaffold has a persistent long-term memory powered by a vector database. It can store, search, and retrieve information across sessions using semantic similarity.

## How it works

When scaffold remembers something, it:

1. Generates a vector embedding of the content (via OpenAI's embedding API or a local embedding service)
2. Stores the embedding in an HNSWLIB vector index for fast similarity search
3. Stores the content and metadata as JSON files
4. Persists everything to `~/.local/scaffold/`

When scaffold needs to recall information, it embeds the query and finds the most semantically similar memories.

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

### `recall_memories`

Searches for relevant memories using semantic similarity.

```
"What database does this project use?"
```

Parameters:
- `query` (required) -- What to search for
- `k` (optional, default 5) -- Number of results to return

Returns memories ranked by similarity score (0-1) with their metadata.

### `forget_memory`

Deletes a specific memory by ID.

## Interactive memory management

Use the `/memory` slash command for direct memory management:

```bash
# List all memories
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
/memory stats long_term_memory
```

## Context enhancement

Scaffold automatically enhances each prompt with relevant memories. Before sending your message to the LLM, it:

1. Embeds your message
2. Searches long-term memory for relevant entries
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

Uses `text-embedding-3-small` (1536 dimensions) by default. Configure with:

```json
{
  "embedding_api_url": "https://api.openai.com/v1/embeddings",
  "embedding_model": "text-embedding-3-small"
}
```

### Local embeddings

Point at a local embedding server (LM Studio, Ollama):

```json
{
  "embedding_api_url": "http://localhost:1234/v1/embeddings",
  "embedding_model": "all-MiniLM-L6-v2"
}
```

Supported local models include Qwen3-Embedding, all-MiniLM, all-mpnet, and others. Dimensions are auto-detected.
