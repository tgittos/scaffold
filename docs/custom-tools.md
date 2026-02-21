# Custom Tools

Scaffold's tool system is extensible. You can add custom Python tools and override worker role prompts.

## Python tools

Scaffold includes 11 built-in Python tools (file I/O, shell, web fetch, package management). You can add your own.

### Adding a custom tool

Place a `.py` file in `~/.local/scaffold/tools/`:

```python
def my_tool(query: str, max_results: int = 10) -> str:
    """Search a custom database.

    Args:
        query: The search query to execute
        max_results: Maximum number of results to return

    Returns:
        JSON string with search results
    """
    # Your implementation here
    import json
    results = do_search(query, max_results)
    return json.dumps(results)
```

Scaffold automatically:
- Discovers the file on startup
- Introspects the function signature and docstring
- Registers it as an LLM-callable tool
- Generates the tool schema from type annotations and docstrings

### Tool conventions

- **One function per file** -- the main function becomes the tool
- **Type annotations** -- used to generate the parameter schema (`str`, `int`, `float`, `bool`)
- **Google-style docstrings** -- `Args:` section defines parameter descriptions, main docstring becomes the tool description
- **Return a string** -- tool results are returned as strings to the LLM

### Built-in Python tools

These are extracted to `~/.local/scaffold/tools/` on first run:

| Tool | Description |
|------|-------------|
| `read_file` | Read file contents |
| `write_file` | Write/create files |
| `append_file` | Append to files |
| `file_info` | Get file metadata (size, permissions, timestamps) |
| `list_dir` | List directory contents with filtering |
| `search_files` | Search files by content or pattern |
| `apply_delta` | Apply unified diff patches |
| `shell` | Execute shell commands with timeout |
| `web_fetch` | Fetch and process web content |
| `pip_install` | Install pure-Python packages from PyPI |
| `pip_list` | List installed Python packages |

### Python extensions

Custom tools have access to three C extension modules:

- **`_ralph_http`** -- HTTP client (get, post, download)
- **`_ralph_verified_io`** -- TOCTOU-safe file operations
- **`_ralph_sys`** -- System utilities (get_executable_path, get_app_home)

## Worker role prompts

Workers are assigned roles that control their behavior. Override built-in roles or create new ones.

### Overriding a built-in role

Create a markdown file at `~/.local/scaffold/prompts/<role>.md`:

```markdown
# Implementation Role

You are an implementation worker. Follow these project-specific conventions:

- Use TypeScript strict mode
- All functions must have JSDoc comments
- Use Zod for runtime validation
- Prefer functional programming patterns
- Write tests alongside implementation
```

### Built-in roles

| Role | File to override |
|------|-----------------|
| `implementation` | `~/.local/scaffold/prompts/implementation.md` |
| `testing` | `~/.local/scaffold/prompts/testing.md` |
| `code_review` | `~/.local/scaffold/prompts/code_review.md` |
| `architecture_review` | `~/.local/scaffold/prompts/architecture_review.md` |
| `design_review` | `~/.local/scaffold/prompts/design_review.md` |
| `pm_review` | `~/.local/scaffold/prompts/pm_review.md` |

If no override file exists, scaffold falls back to its built-in prompt for that role.

### Custom roles

Create new roles by adding files with any name. Role names must be alphanumeric with hyphens and underscores (e.g., `security_audit.md`, `performance-review.md`).
