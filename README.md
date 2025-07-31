# Ralph

A portable command-line AI assistant that brings the power of language models directly to your terminal. Built with Cosmopolitan C, Ralph runs on Linux, Windows, macOS, FreeBSD, and more from a single binary.

## What is Ralph?

Ralph is an HTTP client for AI language models that enables developers to:
- Query OpenAI, Anthropic, and any OpenAI-compatible API
- Execute shell commands through AI-driven workflows
- Maintain persistent conversation context across sessions
- Work with both cloud and local language models
- Run on any platform without installation or dependencies

## Quick Start

```bash
# Ask a question
./ralph "What files are in this directory?"

# Interactive mode
./ralph

# Debug mode
./ralph --debug "Show me the Makefile"
```

## Key Features

### Universal Portability
- **Single binary** runs on Linux, Windows, macOS, FreeBSD, NetBSD, and OpenBSD
- **No installation required** - just download and run
- **No dependencies** - everything is statically linked
- Built with [Cosmopolitan Libc](https://cosmos.zip) for true write-once, run-anywhere capability

### Tool Calling & Automation
- **Shell command execution** - Ralph can run commands and analyze their output
- **File operations** - Read files, analyze code, understand project structure  
- **Web browsing** - Fetch and analyze web content (via embedded links browser)
- **OpenAI function calling protocol** - Compatible with tool-use patterns

### Flexible Model Support
- **OpenAI API** - GPT-4, GPT-4 Turbo, GPT-3.5
- **Anthropic API** - Claude 3.5 Sonnet, Claude 3 Opus, Claude 3 Haiku
- **Local models** - LM Studio, Ollama, llama.cpp server
- **Any OpenAI-compatible endpoint** - Custom deployments, alternative providers
- **Automatic parameter adaptation** - Handles API differences transparently

### Developer-Focused Design
- **Interactive and batch modes** - Use in scripts or as an interactive assistant
- **Persistent conversations** - Continue where you left off
- **Custom system prompts** - Tailor behavior for specific tasks
- **Environment-based config** - Easy integration with different projects

## Installation

### Download Pre-built Binary

```bash
# Download the latest release
wget https://github.com/bluetongueai/ralph/releases/latest/download/ralph
chmod +x ralph
./ralph --help
```

### Build from Source

```bash
git clone https://github.com/bluetongueai/ralph
cd ralph
make
```

The build process automatically:
- Downloads and builds all dependencies (libcurl, mbedTLS)
- Creates a portable binary that runs anywhere
- Runs comprehensive test suite

## Configuration

Ralph uses environment variables and `.env` files for configuration:

```bash
# OpenAI API
OPENAI_API_KEY=sk-your-api-key
API_URL=https://api.openai.com/v1/chat/completions
MODEL=gpt-4o
CONTEXT_WINDOW=128000

# Anthropic API
ANTHROPIC_API_KEY=sk-ant-your-api-key
API_URL=https://api.anthropic.com/v1/messages
MODEL=claude-3-5-sonnet-20241022
CONTEXT_WINDOW=200000

# Local LM Studio
API_URL=http://localhost:1234/v1/chat/completions
MODEL=qwen/qwen-2.5-coder-32b
CONTEXT_WINDOW=32768

# Ollama
API_URL=http://localhost:11434/v1/chat/completions  
MODEL=llama3.3:latest
CONTEXT_WINDOW=131072
```

### Configuration Options

| Variable | Description | Default |
|----------|-------------|---------|
| `API_URL` | API endpoint URL | `https://api.openai.com/v1/chat/completions` |
| `MODEL` | Model identifier | `gpt-4o-mini` |
| `CONTEXT_WINDOW` | Maximum context size | `8192` |
| `MAX_TOKENS` | Max response tokens | Auto-calculated |
| `OPENAI_API_KEY` | OpenAI API key | None |
| `ANTHROPIC_API_KEY` | Anthropic API key | None |

## Usage Examples

### Software Development

```bash
# Code analysis
./ralph "Analyze the memory management in http_client.c"

# Debugging assistance  
./ralph "Help me debug why this test is failing" < test_output.log

# Architecture decisions
./ralph "What's the best way to add async support to this codebase?"
```

### System Administration

```bash
# Log analysis
./ralph "Analyze these nginx logs for anomalies" < /var/log/nginx/access.log

# Script generation
./ralph "Write a bash script to backup MySQL databases to S3"

# Troubleshooting
./ralph "Debug why this systemd service keeps failing"
```

### Interactive Development

```bash
$ ./ralph
Ralph Interactive Mode
Type 'quit' or 'exit' to end the conversation.

> Show me all TODO comments in the codebase
[Ralph executes grep/ripgrep and analyzes results]

> Can you help me implement the first one?
[Ralph provides implementation guidance with code]

> Run the tests to see if it works
[Ralph executes tests and explains results]
```

## Advanced Features

### Custom System Prompts

Create a `PROMPT.md` file in your project:

```markdown
You are an expert Rust developer. Focus on memory safety, 
error handling, and idiomatic Rust patterns. When reviewing code,
pay special attention to lifetime annotations and trait implementations.
```

### Conversation Persistence

Ralph automatically saves conversation history to `CONVERSATION.md`:
- Continue conversations across sessions
- Review previous interactions
- Share context with team members
- Track problem-solving progress

## Integration Examples

### Shell Scripts

```bash
#!/bin/bash
# Get AI help for command output
error_output=$(make 2>&1)
if [ $? -ne 0 ]; then
    echo "$error_output" | ./ralph "Explain this build error and suggest fixes"
fi
```

### Git Hooks

```bash
#!/bin/sh
# .git/hooks/prepare-commit-msg
commit_msg=$(./ralph "Generate a commit message for these changes: $(git diff --staged)")
echo "$commit_msg" > $1
```

### CI/CD Pipelines

```yaml
- name: AI Code Review
  run: |
    git diff origin/main..HEAD | ./ralph "Review these changes for potential issues"
```

## Platform Support

Ralph has been tested on:
- **Linux**: Ubuntu, Debian, Fedora, Arch (x86_64, ARM64)
- **Windows**: Windows 10/11 (native, no WSL required)
- **macOS**: Intel and Apple Silicon
- **BSD**: FreeBSD, OpenBSD, NetBSD
- **Other**: Any platform Cosmopolitan supports

## Building for Development

```bash
# Standard build
make

# Quick rebuild (skips dependency building)
make ralph

# Run tests
make test

# Memory safety verification
make check-valgrind

# Clean build
make clean
```

## License

Ralph is built with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) (ISC License).
