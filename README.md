# Ralph

An HTTP client application written in C for making OpenAI API requests, built with Cosmopolitan Libc.

## Building

This project uses [Cosmopolitan Libc](https://cosmos.zip) for cross-platform compilation.

### Prerequisites

- [Cosmopolitan Libc](https://cosmos.zip) compiler toolchain
- GNU Autotools (autoconf, automake, autotools-dev, m4)

### Quick Build

The simplest way to build:

```bash
cosmocc src/main.c -o ralph
```

### Using Autotools

For a more traditional autotools build:

```bash
# Generate build files (if configure doesn't exist)
autoreconf -i

# Configure the build
./configure

# Build (Note: may fail due to cosmocc space-in-arguments limitation)
make
```

**Note:** The autotools build may fail because cosmocc doesn't support arguments containing spaces. Use the direct compilation method above instead.

### Output

The build produces multiple executable formats:
- `ralph` - Main executable
- `ralph.aarch64.elf` - Native ARM64 executable
- `ralph.com.dbg` - Debug symbols

## Running

```bash
export OPENAI_API_KEY="your-api-key-here"
./ralph
```

Output:
```
Making OpenAI API request to https://api.openai.com/v1/chat/completions
POST data: {"model": "gpt-3.5-turbo","messages": [{"role": "user","content": "Hello from C! Please respond with a brief greeting."}],"max_tokens": 100}

OpenAI API Response:
{"id":"chatcmpl-...","object":"chat.completion",...}
```

## Development Container

This project includes a devcontainer configuration that sets up:
- Debian bookworm-slim base
- Cosmopolitan Libc toolchain
- GNU Autotools
- Node.js and Claude Code CLI