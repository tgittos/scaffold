# Ralph - Software Specification Document

**Version:** 1.1
**Status:** Active Development
**Platform:** Cross-platform (Linux, macOS, Windows, FreeBSD, OpenBSD, NetBSD)

---

## 1. Executive Summary

Ralph is a portable command-line AI assistant designed for software engineers and developers. It provides a consistent interface across multiple LLM providers with an extensible tools system, persistent semantic memory capabilities, and document processing features. The application is built using Cosmopolitan C for maximum portability, producing a single binary that runs natively on all major operating systems and architectures.

### 1.1 Key Value Propositions

- **Universal Portability**: Single binary runs on Linux, macOS, Windows, and BSD systems
- **Multi-Provider LLM Support**: Seamlessly switch between Anthropic, OpenAI, and local LLM servers
- **Persistent Memory**: Long-term semantic memory that persists across sessions
- **Extensible Tool System**: Plugin architecture for file operations, shell commands, and custom tools
- **Subagent Delegation**: Spawn background subagents to handle parallel tasks with isolated contexts
- **MCP Integration**: Model Context Protocol support for connecting to external tool servers
- **Developer-Focused**: Built for software engineering workflows with file, shell, and code tools

---

## 2. Functional Requirements

### 2.1 User Interface

#### 2.1.1 Command Line Interface

**Single Message Mode**
```bash
ralph "Your question or request here"
ralph --debug "Question with API debugging enabled"
```

**Interactive Mode**
```bash
ralph              # Starts interactive REPL
ralph --debug      # Interactive mode with API debugging
```

**Interactive Mode Commands**
| Command | Action |
|---------|--------|
| `quit` / `exit` | Exit the application |
| `Ctrl+D` | Exit the application (EOF) |
| `/memory <command>` | Memory management slash commands |

#### 2.1.2 Slash Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `/memory help` | - | Display help message |
| `/memory list` | `[index_name]` | List chunks from index (default: long_term_memory) |
| `/memory search` | `<query>` | Search chunks by content/metadata |
| `/memory show` | `<chunk_id>` | Display full details of a chunk |
| `/memory edit` | `<chunk_id> <field> <value>` | Edit chunk metadata |
| `/memory indices` | - | List all available indices with stats |
| `/memory stats` | `[index_name]` | Show statistics for index |

### 2.2 LLM Provider Support

#### 2.2.1 Supported Providers

| Provider | Detection Method | API Format | Features |
|----------|-----------------|------------|----------|
| **Anthropic** | URL contains `api.anthropic.com` | Messages API | Tool calling, thinking tags |
| **OpenAI** | URL contains `api.openai.com` | Chat Completions | Tool calling, JSON mode |
| **Local AI** | All other URLs (e.g., `localhost:1234`) | OpenAI-compatible | LMStudio, Ollama support |

#### 2.2.2 Provider Auto-Detection

The system automatically selects the appropriate provider based on the configured `api_url`:
- Anthropic API URL → Anthropic provider with `x-api-key` header
- OpenAI API URL → OpenAI provider with `Bearer` token
- Other URLs → Local AI provider with OpenAI-compatible format

#### 2.2.3 Supported Models

| Provider | Models | Max Context |
|----------|--------|-------------|
| **Anthropic** | Claude family (claude-3-*, claude-2-*) | 200,000 tokens |
| **OpenAI** | GPT-4, GPT-4o, O1, O1-mini, GPT-3.5-turbo | 128,000 tokens |
| **DeepSeek** | DeepSeek models | Variable |
| **Qwen** | Qwen models | Variable |
| **Local** | Any OpenAI-compatible model | Configurable |

### 2.3 Tool System

#### 2.3.1 File Tools (7 tools)

| Tool | Parameters | Description |
|------|------------|-------------|
| `file_read` | `file_path` (required), `start_line`, `end_line`, `max_tokens` | Read file contents with optional line range and smart truncation |
| `file_write` | `file_path` (required), `content` (required), `create_backup` | Write content to file with optional backup |
| `file_append` | `file_path` (required), `content` (required) | Append content to existing file |
| `file_list` | `directory_path` (required), `pattern`, `include_hidden` | List directory contents with optional filtering |
| `file_search` | `search_path` (required), `pattern` (required), `case_sensitive`, `max_tokens`, `max_results` | Search for text patterns in files |
| `file_info` | `file_path` (required) | Get detailed file information and metadata |
| `file_delta` | `file_path` (required), `operations` (required), `create_backup` | Apply delta patch operations for efficient partial updates |

**File Tool Features:**
- Path validation and directory traversal prevention
- Maximum file size limit: 10MB
- Smart content truncation based on token budget
- Automatic backup creation on overwrite
- Recursive directory listing support

#### 2.3.2 Shell Tool

| Tool | Parameters | Description |
|------|------------|-------------|
| `shell_execute` | `command` (required), `working_directory`, `timeout_seconds`, `capture_stderr` | Execute shell commands on the host system |

**Shell Tool Features:**
- Maximum command length: 8,192 characters
- Maximum timeout: 300 seconds
- Automatic stdout/stderr capture
- Dangerous command detection and blocking
- Working directory support

**Blocked Patterns:**
- `rm -rf /` and `rm -rf /*`
- `mkfs` commands
- `dd if=` (disk operations)
- Fork bombs
- Recursive chmod on root

#### 2.3.3 Memory Tools (3 tools)

| Tool | Parameters | Description |
|------|------------|-------------|
| `remember` | `content` (required), `type`, `source`, `importance`, `ttl` | Store information in long-term semantic memory |
| `recall_memories` | `query` (required) | Search and retrieve relevant memories using hybrid search |
| `forget_memory` | `memory_id` (required) | Delete a specific memory by ID |

**Memory Item Structure:**
```json
{
  "memory_id": 12345,
  "content": "The memory content",
  "type": "fact",
  "source": "conversation",
  "importance": "normal",
  "ttl": null,
  "created_at": "2025-01-19T10:30:00Z",
  "expires_at": null
}
```

**Memory Types and Behavior:**

| Type | Description | Recall Behavior |
|------|-------------|-----------------|
| `correction` | User corrections to AI behavior | **Always included** in context when relevant (bypass k limit) |
| `instruction` | Standing instructions for future sessions | **Always included** in context when relevant (bypass k limit) |
| `preference` | User preferences and settings | **Boosted** relevance score (+0.2) |
| `fact` | Important facts or context | Standard recall |
| `web_content` | Key information from web sources | Standard recall |
| `general` | Default type for unclassified memories | Standard recall |

**Importance Levels and Behavior:**

| Importance | Description | Recall Score Multiplier | Default TTL |
|------------|-------------|------------------------|-------------|
| `critical` | Must never forget | 2.0x | None (permanent) |
| `high` | Important, long-term | 1.5x | None (permanent) |
| `normal` | Standard memory (default) | 1.0x | 90 days |
| `low` | Ephemeral, short-term | 0.75x | 7 days |

**TTL (Time-To-Live) Policies:**

- Memories can have an explicit `ttl` parameter (in seconds) or use the default based on importance
- Expired memories are automatically excluded from recall results
- Expired memories are lazily purged during recall operations (not immediately deleted)
- `critical` and `high` importance memories default to permanent (no expiration)
- Setting `ttl: 0` explicitly creates a permanent memory regardless of importance

**Automatic Memory Recall:**

On each user message, the system automatically:
1. Generates query embedding from user message
2. Performs hybrid search (text matching + semantic similarity)
3. Filters out expired memories (based on TTL)
4. Applies importance score multipliers to rank results
5. Applies type-based score boosts
6. **Always includes** matching `correction` and `instruction` type memories (not counted against limit)
7. Returns top **5** results (plus any forced-include types)
8. Injects results into the system prompt under `# Relevant Memories`

#### 2.3.4 Todo Tools (Task Management)

| Tool | Parameters | Description |
|------|------------|-------------|
| `todo_add` | `content` (required), `priority` (optional) | Add a new todo item |
| `todo_update` | `id` (required), `status`, `priority`, `content` | Update a single todo item |
| `todo_remove` | `id` (required) | Remove a todo item by ID |
| `todo_list` | `status` (optional), `priority` (optional) | List todos with optional filtering |

**Tool Details:**

**`todo_add`** - Create a new task
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `content` | string | Yes | - | Task description (max 512 chars) |
| `priority` | string | No | `medium` | Priority level: `low`, `medium`, `high` |

Returns: `{ "id": "generated-id", "content": "...", "priority": "...", "status": "pending" }`

**`todo_update`** - Update an existing task
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `id` | string | Yes | The todo item ID to update |
| `status` | string | No | New status: `pending`, `in_progress`, `completed` |
| `priority` | string | No | New priority: `low`, `medium`, `high` |
| `content` | string | No | New task description |

Returns: `{ "id": "...", "updated_fields": [...] }`

**`todo_remove`** - Delete a task
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `id` | string | Yes | The todo item ID to remove |

Returns: `{ "deleted_id": "..." }`

**`todo_list`** - List and filter tasks
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `status` | string | No | Filter by status: `pending`, `in_progress`, `completed`, or `all` (default) |
| `priority` | string | No | Minimum priority filter: `low`, `medium`, `high` |

Returns: `{ "todos": [...], "count": N }`

**Todo Item Structure:**
```json
{
  "id": "unique-identifier",
  "content": "Task description",
  "status": "pending|in_progress|completed",
  "priority": "low|medium|high",
  "created_at": "ISO-8601 timestamp",
  "updated_at": "ISO-8601 timestamp"
}
```

**Status Definitions:**
| Status | Description |
|--------|-------------|
| `pending` | Task has not been started |
| `in_progress` | Task is currently being worked on |
| `completed` | Task has been finished |

**Priority Definitions:**
| Priority | Description |
|----------|-------------|
| `low` | Non-urgent, can be deferred |
| `medium` | Normal priority (default) |
| `high` | Urgent, should be addressed soon |

**Constraints:**
- Maximum 100 todo items per session
- Content limited to 512 characters
- IDs are auto-generated and unique within session

#### 2.3.5 Vector Database Tools (11+ tools)

**Index Management:**
| Tool | Description |
|------|-------------|
| `vector_db_create_index` | Create a new vector index |
| `vector_db_delete_index` | Delete an existing index |
| `vector_db_list_indices` | List all available indices |

**Vector Operations:**
| Tool | Description |
|------|-------------|
| `vector_db_add_vector` | Add a vector to an index |
| `vector_db_update_vector` | Update an existing vector |
| `vector_db_delete_vector` | Delete a vector by ID |
| `vector_db_get_vector` | Retrieve a vector by ID |
| `vector_db_search` | K-nearest neighbor similarity search |

**Text Operations:**
| Tool | Description |
|------|-------------|
| `vector_db_add_text` | Add text with auto-generated embedding |
| `vector_db_add_chunked_text` | Add text with automatic chunking |
| `vector_db_add_pdf_document` | Process and index PDF documents |

#### 2.3.6 PDF Processing Tool

| Tool | Parameters | Description |
|------|-------------|
| `process_pdf_document` | `file_path`, `index_name` | Extract text from PDFs and index in vector database |

**PDF Processing Pipeline:**
1. Text extraction using PDFio library
2. Intelligent document chunking
3. Embedding generation
4. Vector database indexing
5. Metadata storage

#### 2.3.7 Subagent Tool

| Tool | Parameters | Description |
|------|------------|-------------|
| `subagent` | `task` (required), `context` (optional) | Spawn a background subprocess to handle a delegated task |

**Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `task` | string | Yes | The task description/prompt for the subagent to execute |
| `context` | string | No | Optional additional context to provide to the subagent |

**Execution Model:**

- **Background Execution**: Subagents run asynchronously in the background, allowing the parent to continue processing
- **Isolated Context**: Subagents start with a fresh conversation context; they do not inherit the parent's conversation history
- **No Nesting**: Subagents CANNOT spawn their own subagents (recursion depth is limited to 1)
- **Tool Access**: Subagents have access to all standard tools except `subagent`

**Return Value:**

```json
{
  "subagent_id": "unique-identifier",
  "status": "running|completed|failed",
  "result": "output from subagent (when completed)"
}
```

**Use Cases:**

- Parallel research tasks (e.g., searching multiple codebases simultaneously)
- Long-running operations that shouldn't block the main conversation
- Decomposing complex tasks into independent subtasks
- Background file processing or analysis

**Lifecycle:**

1. Parent invokes `subagent` with a task description
2. Ralph spawns a new process with isolated context
3. Subagent executes the task using available tools (except `subagent`)
4. Parent can query subagent status using `subagent_status`
5. Subagent result is returned when task completes or via status query

**Status Query Tool:**

| Tool | Parameters | Description |
|------|------------|-------------|
| `subagent_status` | `subagent_id` (required), `wait` (optional) | Query the status of a running subagent |

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `subagent_id` | string | Yes | - | The ID returned from the `subagent` tool |
| `wait` | boolean | No | false | If true, block until subagent completes |

**Status Response:**

```json
{
  "subagent_id": "unique-identifier",
  "status": "running|completed|failed",
  "progress": "optional progress message",
  "result": "final output (only when completed)",
  "error": "error message (only when failed)"
}
```

#### 2.3.9 Links Tool (Web Fetch)

| Tool | Description |
|------|-------------|
| `web_fetch` | Fetch and process web content |

### 2.4 Memory System

#### 2.4.1 Vector Database

**Backend:** HNSWLIB (Hierarchical Navigable Small World graphs)

**Index Configurations:**
| Index Type | Max Elements | M | ef_construction | Use Case |
|------------|--------------|---|-----------------|----------|
| Memory | 100,000 | 16 | 200 | Long-term semantic memory |
| Documents | 50,000 | 32 | 400 | PDF and document storage |

**Distance Metric:** Cosine similarity

#### 2.4.2 Embedding Providers

| Provider | Model | Dimensions | URL Pattern |
|----------|-------|------------|-------------|
| **OpenAI** | text-embedding-3-small | 1,536 | api.openai.com |
| **OpenAI** | text-embedding-3-large | 3,072 | api.openai.com |
| **Local** | Variable | Variable | localhost or other |

**Supported Local Embedding Models:**
- Qwen3-Embedding
- all-MiniLM
- all-mpnet
- Other OpenAI-compatible embedding APIs

#### 2.4.3 Storage Architecture

**Storage Location:** `~/.local/ralph/`

| Type | Path | Format |
|------|------|--------|
| Vector Indices | `~/.local/ralph/*.idx` | HNSWLIB binary |
| Index Metadata | `~/.local/ralph/*.meta` | JSON |
| Documents | `~/.local/ralph/documents/{index}/doc_{id}.json` | JSON |
| Chunk Metadata | `~/.local/ralph/metadata/{index}/chunk_{id}.json` | JSON |

#### 2.4.4 Automatic Memory Recall

The system automatically retrieves relevant memories on each user message:

**Recall Pipeline:**
1. Generate query embedding from user message
2. Perform hybrid search (text matching + semantic similarity)
3. Filter out expired memories (based on TTL)
4. Apply importance score multipliers (critical: 2.0x, high: 1.5x, normal: 1.0x, low: 0.75x)
5. Apply type-based score boosts (preference: +0.2)
6. **Always include** relevant `correction` and `instruction` memories (bypass limit)
7. Return top **5** results (plus any forced-include types)
8. Inject into system prompt under `# Relevant Memories` section

**Scoring Formula:**
```
final_score = base_score × importance_multiplier + type_boost
```

Where:
- `base_score` = text match (1.0) or semantic similarity (1.0 - distance)
- Memories appearing in both text and semantic results get: `(score1 + score2) / 2 + 0.1`

**Memory Expiration Check:**
```
is_expired = (expires_at != NULL) && (current_time > expires_at)
```

Expired memories are excluded from results and lazily deleted during recall.

### 2.5 MCP (Model Context Protocol) Integration

#### 2.5.1 Supported Transports

| Transport | Description | Use Case |
|-----------|-------------|----------|
| **STDIO** | Local process with stdin/stdout | Local tool servers |
| **HTTP** | REST API endpoint | Remote tool servers |
| **SSE** | Server-Sent Events | Streaming responses |

#### 2.5.2 Configuration

MCP servers are configured in `ralph.config.json`:

```json
{
  "mcpServers": {
    "example-server": {
      "command": "/path/to/server",
      "args": ["--flag"],
      "env": { "KEY": "value" }
    }
  }
}
```

#### 2.5.3 Tool Discovery

- Tools are discovered via JSON-RPC `tools/list` at connection time
- Tools are registered as `mcp_{servername}_{toolname}`
- Supports environment variable expansion: `${VAR}` and `${VAR:-default}`
- Graceful degradation when MCP servers are unavailable

### 2.6 Session Management

#### 2.6.1 Conversation Persistence

- Conversations are tracked in memory during session
- Tool call sequences are preserved for context
- Conversation history is used for API requests

#### 2.6.2 Token Management

**Context Window Optimization:**
- Automatic token allocation based on model capabilities
- Dynamic response token calculation
- Safety buffer for system overhead

**Conversation Compaction:**
- Background compaction triggered at 40% context usage
- Emergency compaction when response tokens insufficient
- Tool sequences preserved during compaction

#### 2.6.3 Context Retrieval

- Automatic retrieval of relevant context from vector database
- Top-5 relevant documents injected into prompt
- Semantic similarity scoring

---

## 3. Configuration

### 3.1 Configuration File

**File:** `ralph.config.json`

**Search Priority:**
1. `./ralph.config.json` (current directory)
2. `~/.local/ralph/config.json` (user directory)

**Schema:**
```json
{
  "api_url": "https://api.openai.com/v1/chat/completions",
  "model": "gpt-4o-mini",
  "anthropic_api_key": "",
  "openai_api_key": "",
  "openai_api_url": "",
  "embedding_api_url": "",
  "embedding_model": "",
  "system_prompt": "",
  "context_window": 8192,
  "max_tokens": -1,
  "max_subagents": 5,
  "subagent_timeout": 300,
  "mcpServers": {}
}
```

### 3.2 Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `api_url` | string | `https://api.openai.com/v1/chat/completions` | LLM API endpoint |
| `model` | string | `gpt-4o-mini` | Model identifier |
| `anthropic_api_key` | string | "" | Anthropic API key |
| `openai_api_key` | string | "" | OpenAI API key |
| `openai_api_url` | string | "" | Alternative OpenAI-compatible URL |
| `embedding_api_url` | string | "" | Separate embedding API URL |
| `embedding_model` | string | "" | Embedding model name |
| `system_prompt` | string | "" | Custom system prompt |
| `context_window` | integer | 8192 | Context window size (auto-detected from model) |
| `max_tokens` | integer | -1 | Max response tokens (-1 for auto) |
| `max_subagents` | integer | 5 | Maximum concurrent subagents per session |
| `subagent_timeout` | integer | 300 | Subagent execution timeout in seconds |

### 3.3 Environment Variables

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI API key (overrides config) |
| `ANTHROPIC_API_KEY` | Anthropic API key (overrides config) |

**Priority:** Environment variables override configuration file values.

### 3.4 System Prompt

**File:** `PROMPT.md` (optional)

Custom user instructions can be placed in `PROMPT.md` in the current directory. This content is appended to the built-in system prompt.

**Built-in System Prompt Features:**
- Adaptive behavior framework (simple vs. complex requests)
- Context sensitivity guidelines
- Tool usage guidelines
- Memory management instructions
- Todo management guidelines

---

## 4. Technical Specifications

### 4.1 Build System

| Target | Description |
|--------|-------------|
| `make` | Build the complete application |
| `make clean` | Remove build artifacts |
| `make distclean` | Remove everything including dependencies |
| `make test` | Run the full test suite |
| `make check-valgrind` | Run Valgrind memory analysis |

### 4.2 Dependencies

**Core Libraries:**
- **Cosmopolitan Libc** - Cross-platform C runtime
- **libcurl** - HTTP client
- **mbedTLS** - TLS/SSL support
- **cJSON** - JSON parsing
- **PDFio** - PDF text extraction
- **HNSWLIB** - Vector similarity search
- **readline** - Command-line editing

**Test Framework:**
- **Unity** - Unit testing framework (vendored)

### 4.3 Architecture Layers

```
┌─────────────────────────────────────────────────────┐
│                   CLI Layer                         │
│            (main.c, memory_commands.c)              │
├─────────────────────────────────────────────────────┤
│                   Core Layer                        │
│                   (ralph.c)                         │
├──────────┬──────────┬──────────┬──────────┬────────┤
│ LLM      │ Tools    │ Session  │ MCP      │ Vector │
│ Provider │ System   │ Manager  │ Client   │ DB     │
├──────────┴──────────┴──────────┴──────────┴────────┤
│                  Network Layer                      │
│              (http_client, api_common)              │
├─────────────────────────────────────────────────────┤
│               External Libraries                    │
│         (curl, mbedTLS, HNSWLIB, PDFio)            │
└─────────────────────────────────────────────────────┘
```

### 4.4 Design Patterns

| Pattern | Usage |
|---------|-------|
| **Registry Pattern** | Provider, tool, and model registration |
| **Singleton Pattern** | Vector DB service, embeddings service, config |
| **Builder Pattern** | Tool result construction |
| **Strategy Pattern** | LLM provider abstraction |
| **Plugin Architecture** | Tool and provider extensibility |

### 4.5 Thread Safety

- Vector database service uses mutex protection
- Singleton services are thread-safe
- Concurrent index access is supported

### 4.6 Memory Safety

- Defensive programming with parameter validation
- All pointers initialized before use
- Comprehensive memory cleanup on error paths
- Valgrind-verified memory safety

---

## 5. Security Considerations

### 5.1 Input Validation

- Path validation prevents directory traversal
- Shell command validation blocks dangerous operations
- JSON input is parsed safely with cJSON

### 5.2 API Key Security

- API keys can be stored in config file or environment
- Environment variables override config for deployment flexibility
- Keys are not logged (except in debug mode)

### 5.3 Shell Execution

**Blocked Operations:**
- Recursive delete of root filesystem
- Disk formatting operations
- Fork bombs
- Dangerous permission changes

**Safety Measures:**
- Command length limits
- Execution timeout enforcement
- Output size limits

### 5.4 File Operations

- Maximum file size: 10MB
- Path length limits enforced
- Directory traversal (`..`) blocked
- Null byte injection prevented

### 5.5 Subagent Security

**Resource Controls:**
- Maximum concurrent subagents per session: configurable (default: 5)
- Subagent execution timeout: configurable (default: 300 seconds)
- No nesting: subagents cannot spawn subagents (prevents fork bombs)

**Isolation:**
- Fresh conversation context (no parent history leakage)
- Independent process space
- Shared configuration (API keys) but isolated execution

**Tool Restrictions:**
- `subagent` and `subagent_status` tools are disabled within subagent processes
- All other tool safety measures apply (shell blocking, path validation, etc.)

---

## 6. Data Flow

### 6.1 Message Processing Flow

```
User Input
    │
    ▼
Session Initialization
    │
    ▼
Token Allocation
    │
    ▼
Memory Recall (automatic)
    │
    ▼
Payload Construction
    │
    ▼
API Request
    │
    ▼
Response Parsing
    │
    ├── Tool Calls Detected? ──▶ Tool Execution Loop
    │                                    │
    │                                    ▼
    │                           Execute Tools
    │                                    │
    │                                    ▼
    │                           Store Results
    │                                    │
    │                                    ▼
    │                           Continue API Request
    │                                    │
    │◀─────────────────────────────────────┘
    │
    ▼
Display Response
    │
    ▼
Save Conversation
```

### 6.2 Memory Storage Flow

```
Remember Request
    │
    ├── content (required)
    ├── type (default: general)
    ├── source (default: conversation)
    ├── importance (default: normal)
    └── ttl (optional, in seconds)
    │
    ▼
Validate Content
    │
    ▼
Calculate Expiration
    │  • If ttl=0: expires_at = NULL (permanent)
    │  • If ttl provided: expires_at = now + ttl
    │  • Else use importance default:
    │      critical/high: NULL (permanent)
    │      normal: now + 90 days
    │      low: now + 7 days
    │
    ▼
Generate Embedding
    │
    ▼
Store Vector in HNSWLIB
    │
    ▼
Store Metadata in JSON
    │  • memory_id, content, type, source
    │  • importance, created_at, expires_at
    │
    ▼
Return Memory ID
```

### 6.3 Memory Recall Flow

```
User Query
    │
    ▼
Generate Query Embedding
    │
    ├── Text Search ──────────────────┐
    │                                 │
    ├── Semantic Search ─────────────┐│
    │                                ││
    ▼                                ▼▼
Merge Results (deduplicate, boost duplicates +0.1)
    │
    ▼
Filter Expired Memories (expires_at < now)
    │
    ▼
Apply Importance Multipliers
    │  • critical: score × 2.0
    │  • high: score × 1.5
    │  • normal: score × 1.0
    │  • low: score × 0.75
    │
    ▼
Apply Type Boosts
    │  • preference: score + 0.2
    │
    ▼
Partition by Type
    │
    ├── correction/instruction ──▶ Always Include (if relevant)
    │
    └── other types ──▶ Sort by Score, Take Top 5
    │
    ▼
Lazy Purge Expired Memories
    │
    ▼
Inject into System Prompt
```

### 6.4 Subagent Execution Flow

```
Parent Ralph Session
    │
    ▼
subagent Tool Called
    │
    ├── task: "Search for authentication code"
    └── context: "Focus on OAuth implementations"
    │
    ▼
Generate Unique Subagent ID
    │
    ▼
Fork New Process
    │
    ├─────────────────────────────────────┐
    │ Parent (continues)                  │ Subagent (background)
    │    │                                │    │
    │    ▼                                │    ▼
    │ Return {subagent_id, status}        │ Initialize Fresh Session
    │    │                                │    │
    │    ▼                                │    ▼
    │ Continue Processing                 │ Execute Task
    │    │                                │    │
    │    ▼                                │    ├── File Tools ✓
    │ [Optional] subagent_status          │    ├── Shell Tools ✓
    │    │                                │    ├── Memory Tools ✓
    │    ▼                                │    ├── Vector DB Tools ✓
    │ Query Status/Wait                   │    └── Subagent Tools ✗
    │    │                                │    │
    │    ◀────────────────────────────────│────┘
    │                                     │
    ▼                                     ▼
Receive Subagent Result              Process Terminates
```

**Subagent Constraints:**

```
┌─────────────────────────────────────────────────────────┐
│                    Parent Session                        │
│  ┌─────────────────────────────────────────────────┐    │
│  │              Subagent Process                    │    │
│  │  • Fresh context (no history inheritance)       │    │
│  │  • All tools EXCEPT subagent/subagent_status    │    │
│  │  • Cannot spawn nested subagents                │    │
│  │  • Independent API connection                   │    │
│  │  • Shares config (API keys, model, etc.)        │    │
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

---

## 7. Error Handling

### 7.1 Error Categories

| Category | Handling |
|----------|----------|
| **API Errors** | User-friendly messages with debug option |
| **File Errors** | Specific error codes (not found, permission, too large) |
| **Memory Errors** | Graceful degradation with error messages |
| **Tool Errors** | Tool result includes success flag and error details |
| **Network Errors** | Retry logic with exponential backoff |

### 7.2 Error Codes

**File Operations:**
- `FILE_SUCCESS` - Operation successful
- `FILE_ERROR_NOT_FOUND` - File or directory not found
- `FILE_ERROR_PERMISSION` - Permission denied
- `FILE_ERROR_TOO_LARGE` - File exceeds size limit
- `FILE_ERROR_INVALID_PATH` - Invalid file path
- `FILE_ERROR_MEMORY` - Memory allocation failed
- `FILE_ERROR_IO` - I/O error

**Vector Database:**
- `VECTOR_DB_OK` - Operation successful
- `VECTOR_DB_ERROR_INVALID_PARAM` - Invalid parameter
- `VECTOR_DB_ERROR_ELEMENT_NOT_FOUND` - Element not found
- Additional error codes as defined

---

## 8. Extensibility

### 8.1 Adding New Tools

1. Create tool implementation in `src/tools/`
2. Implement `execute_<tool>_tool_call` function
3. Register tool with `register_tool()` function
4. Add to `register_builtin_tools()` in tools_system.c

### 8.2 Adding New LLM Providers

1. Create provider implementation in `src/llm/providers/`
2. Implement provider detection function
3. Implement request/response formatting
4. Register in provider registry

### 8.3 Adding New Models

1. Create model implementation in `src/llm/models/`
2. Define model capabilities (context window, features)
3. Implement tool call parsing if needed
4. Register in model registry

### 8.4 Custom MCP Servers

1. Configure server in `ralph.config.json`
2. Implement JSON-RPC 2.0 protocol
3. Support `tools/list` for tool discovery
4. Support `tools/call` for tool execution

---

## 9. Performance Characteristics

### 9.1 Startup Time

- Binary startup: <100ms
- Configuration loading: <50ms
- MCP server connection: Variable (async)
- Vector database initialization: <200ms

### 9.2 Memory Usage

- Base memory: ~10-20MB
- Per vector index: Variable based on size
- Conversation history: Linear with message count

### 9.3 Token Efficiency

- Smart content truncation preserves structure
- Background conversation compaction
- Automatic context window optimization

---

## 10. Testing

### 10.1 Test Categories

| Category | Location | Description |
|----------|----------|-------------|
| Unit Tests | `test/` | Component-level testing |
| Integration Tests | `test/` | Multi-component testing |
| Memory Tests | `make check-valgrind` | Memory safety verification |

### 10.2 Test Coverage Areas

- Core functionality (ralph.c)
- Tool execution (all tools)
- LLM provider communication
- Vector database operations
- Session management
- Token management
- Conversation compaction
- MCP client functionality

---

## 11. Deployment

### 11.1 Binary Distribution

The Cosmopolitan build produces a single portable binary:
- `ralph` - Universal binary (runs on all platforms)
- `ralph.com.dbg` - Debug binary (x86_64)
- `ralph.aarch64.elf` - ARM64 ELF binary

### 11.2 First-Time Setup

1. Run `ralph` - creates default config if none exists
2. Set API key via environment or config file
3. Optionally create `PROMPT.md` for custom instructions

### 11.3 Directory Structure

```
~/.local/ralph/
├── config.json           # User configuration
├── *.idx                 # Vector database indices
├── *.meta                # Index metadata
├── documents/            # Document storage
│   └── {index}/
│       └── doc_{id}.json
└── metadata/             # Chunk metadata
    └── {index}/
        └── chunk_{id}.json
```

---

## Appendix A: Tool Reference

### A.1 Complete Tool List

**File Tools (7):**
1. `file_read` - Read file contents
2. `file_write` - Write file contents
3. `file_append` - Append to file
4. `file_list` - List directory
5. `file_search` - Search in files
6. `file_info` - Get file metadata
7. `file_delta` - Apply delta patches

**Shell Tool (1):**
8. `shell_execute` - Execute shell commands

**Memory Tools (3):**
9. `remember` - Store memory with importance and TTL
10. `recall_memories` - Retrieve memories via hybrid search
11. `forget_memory` - Delete memory

**Task Management (4):**
12. `todo_add` - Add a new todo item
13. `todo_update` - Update a single todo item
14. `todo_remove` - Remove a todo item
15. `todo_list` - List and filter todos

**Subagent Tools (2):**
16. `subagent` - Spawn background subagent for delegated tasks
17. `subagent_status` - Query subagent execution status

**PDF Tools (1):**
18. `pdf_extract_text` - Extract text from PDFs

**Vector Database Tools (13):**
19. `vector_db_create_index` - Create vector index
20. `vector_db_delete_index` - Delete vector index
21. `vector_db_list_indices` - List indices
22. `vector_db_add_vector` - Add vector
23. `vector_db_update_vector` - Update vector
24. `vector_db_delete_vector` - Delete vector
25. `vector_db_get_vector` - Get vector
26. `vector_db_search` - Search vectors
27. `vector_db_add_text` - Add text as vector
28. `vector_db_add_chunked_text` - Add chunked text
29. `vector_db_add_pdf_document` - Add PDF to vector DB
30. `vector_db_search_text` - Search by text query
31. `vector_db_search_by_time` - Search by time range

**Web Tools (1):**
32. `web_fetch` - Fetch web content

**External Tools:**
33. `mcp_*` - Dynamic MCP tools (namespaced)

---

## Appendix B: Configuration Examples

### B.1 OpenAI Configuration

```json
{
  "api_url": "https://api.openai.com/v1/chat/completions",
  "model": "gpt-4o",
  "openai_api_key": "sk-..."
}
```

### B.2 Anthropic Configuration

```json
{
  "api_url": "https://api.anthropic.com/v1/messages",
  "model": "claude-3-5-sonnet-20241022",
  "anthropic_api_key": "sk-ant-..."
}
```

### B.3 Local AI Configuration (LMStudio)

```json
{
  "api_url": "http://localhost:1234/v1/chat/completions",
  "model": "local-model",
  "context_window": 32000
}
```

### B.4 Separate Embedding API

```json
{
  "api_url": "https://api.anthropic.com/v1/messages",
  "model": "claude-3-5-sonnet-20241022",
  "anthropic_api_key": "sk-ant-...",
  "embedding_api_url": "https://api.openai.com/v1/embeddings",
  "openai_api_key": "sk-...",
  "embedding_model": "text-embedding-3-small"
}
```

---

## Appendix C: Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2024 | Initial specification (reconstructed from implementation) |
| 1.1 | 2025 | Added subagent tools (`subagent`, `subagent_status`) for background task delegation; Refactored todo tools from bulk `TodoWrite` to granular `todo_add`, `todo_update`, `todo_remove`, `todo_list`; Enhanced memory system with importance-based scoring (2.0x/1.5x/1.0x/0.75x multipliers), type-based behavior (`correction`/`instruction` always included, `preference` boosted), TTL expiration policies (7d/90d/permanent by importance); Updated tool list to 33 tools |

---

*This specification defines the intended behavior of the Ralph software. Features marked as implemented reflect current functionality; features in development are noted accordingly.*
