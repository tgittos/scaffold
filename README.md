# Ralph

A powerful HTTP client for AI language models with advanced tool calling and conversation management. Built with Cosmopolitan C for universal portability across Linux, Windows, macOS, and more.

## Key Features for Developers

**🔧 Tool Integration & Function Calling**
- Full OpenAI API function calling compatibility
- Built-in shell command execution tool with security validation  
- JSON-based tool configuration system
- Automatic tool result handling and follow-up conversations

**💬 Persistent Conversation Management**
- Automatic conversation history tracking in `CONVERSATION.md`
- Multi-turn conversations with full context retention
- System prompt loading from `PROMPT.md` files

**🎯 Smart API Compatibility**
- Works with OpenAI API (`gpt-4`, `gpt-3.5-turbo`, etc.)
- Local LLM servers (LM Studio, Ollama, etc.)
- Automatic parameter adaptation (`max_tokens` vs `max_completion_tokens`)
- Intelligent token calculation and context window management

**🎨 Advanced Output Processing**
- Separates thinking content from main responses
- ANSI color formatting for enhanced readability
- Token usage reporting and conversation statistics
- JSON parsing with robust error handling

**⚡ Production Ready**
- Memory-safe C implementation with comprehensive Valgrind testing
- 1,876 lines of unit tests across 8 test suites
- Single portable binary runs on any platform via Cosmopolitan Libc
- Zero memory leaks, buffer overruns, or undefined behavior

## Building

This project uses [Cosmopolitan Libc](https://cosmos.zip) for cross-platform compilation.

### Prerequisites

- [Cosmopolitan Libc](https://cosmos.zip) compiler toolchain (pre-installed in devcontainer)

### Build with Make

```bash
make
```

This builds the complete application with all dependencies including:
- libcurl for HTTP requests
- mbedTLS for SSL/TLS support
- Comprehensive test suite

### Quick Build (Development)

For rapid development iteration:

```bash
make ralph
```

### Testing

Run the complete test suite:

```bash
make test
```

Run tests with memory safety checks:

```bash
make check-valgrind
```

### Clean Build

```bash
make clean      # Remove build artifacts
make distclean  # Remove dependencies too
make realclean  # Complete clean slate
```

## Usage

### Basic Usage

```bash
./ralph "What is the capital of France?"
```

### Tool Calling Example

Ralph supports function calling for interactive development workflows:

```bash
./ralph "List the files in the current directory and show me the contents of main.c"
```

This automatically:
1. Calls the shell tool to execute `ls`
2. Calls the shell tool to execute `cat main.c` 
3. Provides a summary of the results

### Configuration

Ralph uses `.env` files for configuration:

```bash
# For OpenAI API
OPENAI_API_KEY=sk-your-api-key-here
API_URL=https://api.openai.com/v1/chat/completions
MODEL=gpt-4o
CONTEXT_WINDOW=128000

# For local LM Studio server
API_URL=http://localhost:1234/v1/chat/completions  
MODEL=qwen/qwen3-8b
CONTEXT_WINDOW=4096
# No API key needed for local servers
```

### Advanced Features

**System Prompts**: Create a `PROMPT.md` file for custom system instructions:
```bash
echo "You are a helpful coding assistant specialized in C programming." > PROMPT.md
```

**Conversation History**: Ralph automatically tracks conversation history in `CONVERSATION.md` for multi-turn interactions.

**Tool Configuration**: Define custom tools in `tools.json` for specialized functionality.

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `API_URL` | LLM API endpoint | `https://api.openai.com/v1/chat/completions` |
| `MODEL` | Model name | `o4-mini-2025-04-16` |
| `CONTEXT_WINDOW` | Model's context window size | `8192` |
| `MAX_TOKENS` | Override calculated response tokens | Auto-calculated |
| `OPENAI_API_KEY` | API key (not needed for local servers) | None |

## Developer Workflow Integration

**Interactive Development Sessions**
```bash
./ralph "Analyze this codebase and suggest improvements"
./ralph "Run the tests and explain any failures"  
./ralph "Help me debug the memory issue in http_client.c"
```

**Automated Tool Execution**
Ralph can execute shell commands, analyze output, and provide intelligent responses:
- File system operations (`ls`, `find`, `grep`)
- Build processes (`make`, `gcc`, `valgrind`)
- Git operations (`git status`, `git diff`)
- Testing workflows (`./test_suite`, `make check`)

**Conversation Context**
Each conversation builds on previous context, enabling:
- Multi-step debugging sessions
- Iterative code review and improvement
- Long-form architectural discussions
- Progressive feature development

## Output Format

Ralph provides clean, hierarchical output optimized for terminal usage:

- **Thinking content**: Displayed in dim gray (reasoning process)
- **Main response**: Normal text, prominently displayed
- **Tool execution**: Command output with clear formatting  
- **Token usage**: Statistics shown at bottom

Example output:
```
$ ./ralph "Show me the main function and analyze its memory usage"

[Thinking: I need to examine the main function and look for potential memory issues...]

Here's the main function from src/main.c (lines 154-463):

[Tool: shell command "head -20 src/main.c"]
#include <stdio.h>
#include <stdlib.h>
...

The function properly handles memory management with:
- Cleanup functions called in all exit paths
- Error checking for malloc() calls
- Proper free() for dynamically allocated strings

[tokens: 156 total (89 prompt + 67 completion)]
```

## Architecture

Ralph is built with a modular, testable architecture designed for reliability and extensibility:

**Core Components**
- **`main.c`**: Application entry point with argument parsing, token calculation, and tool orchestration
- **`http_client.c/.h`**: HTTP request handling using libcurl with proper error handling and SSL support
- **`tools_system.c/.h`**: Complete function calling system with OpenAI API compatibility
- **`shell_tool.c/.h`**: Built-in shell execution tool with security validation and timeout support
- **`conversation_tracker.c/.h`**: Persistent conversation history management in Markdown format
- **`output_formatter.c/.h`**: Advanced JSON parsing, response formatting, and ANSI color output
- **`prompt_loader.c/.h`**: System prompt loading from `PROMPT.md` files
- **`env_loader.c/.h`**: Environment variable and `.env` file processing

**Testing & Quality Assurance**
- **1,876 lines of comprehensive unit tests** across 8 test suites
- **Memory safety verification** with Valgrind (zero tolerance for leaks/errors)
- **Modular design** enabling isolated component testing
- **Error path coverage** ensuring robust failure handling

## Development Container

This project includes a devcontainer configuration that sets up:
- Debian bookworm-slim base
- Cosmopolitan Libc toolchain pre-installed at `/opt/cosmos`
- Development tools (make, valgrind, etc.)
- Claude Code CLI for AI-assisted development

## Use Cases

**AI-Assisted Development**
- Interactive code review and analysis
- Automated testing and debugging assistance  
- Architecture planning and design discussions
- Real-time documentation generation

**Local LLM Integration**
- Private, offline AI assistance with local models
- Custom model fine-tuning and experimentation
- Secure development environments without external API calls
- Integration with specialized domain models

**DevOps & Automation**
- Intelligent log analysis and troubleshooting
- Automated code quality assessment
- CI/CD pipeline integration and optimization
- System administration and monitoring assistance

## Contributing

1. **Code Quality**: All code must pass `make test` and `make check-valgrind`
2. **Memory Safety**: Zero tolerance for memory leaks, buffer overruns, or undefined behavior
3. **Testing**: Add comprehensive tests for new functionality (see existing test patterns)
4. **Architecture**: Follow the modular design patterns established in the codebase
5. **Documentation**: Update relevant documentation and code comments

## License

This project uses Cosmopolitan Libc which is ISC licensed. See the [Cosmopolitan License](https://github.com/jart/cosmopolitan/blob/master/LICENSE) for details.