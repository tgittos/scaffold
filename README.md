# Ralph

**The Programming Force Multiplier That Fits in Your Pocket**

Ralph is the AI-powered development companion that transforms ordinary programmers into coding superhumans. Built as a single portable binary that runs anywhere, Ralph doesn't just chat about code - it writes it, fixes it, explains it, and ships it. This is the tool that turns 1x programmers into 10x programmers, and 10x programmers into unstoppable 100x forces of nature.

## The Developer's Secret Weapon

Imagine having a senior developer, systems architect, and research assistant all rolled into one - available 24/7, never gets tired, never judges your code, and costs pennies to run. That's Ralph.

### 🚀 **Code Creation That Actually Works**
Ralph doesn't just generate boilerplate - it crafts production-ready solutions tailored to your exact needs. From complex algorithms to entire application architectures, Ralph understands context, follows your coding style, and delivers code that actually compiles and runs.

### 🔍 **Internet-Powered Research Engine**
Stuck on an obscure API? Need to understand a new framework? Ralph can research the latest documentation, browse GitHub repos, analyze StackOverflow discussions, and synthesize the information into actionable insights - all while you grab coffee.

### 🤝 **Your AI Pair Programming Partner**
Ralph reviews your code with the precision of a senior architect, catches bugs before they hit production, suggests optimizations you never considered, and explains complex codebases like a patient mentor. It's the pair programmer who makes you better, not just faster.

### ⚡ **Automation-First Architecture**
One-shot mode means Ralph integrates seamlessly into CI/CD pipelines, git hooks, and automated workflows. Build scripts that fix themselves. Tests that write documentation. Code reviews that happen in milliseconds, not hours.

## The Interactive Development Experience

The real power of Ralph isn't in one-shot commands - it's in the **conversational development workflow** where you and Ralph build, iterate, and perfect code together in real-time.

```bash
$ ./ralph
Ralph Interactive Mode
Type 'quit' or 'exit' to end the conversation.

> I want to build a REST API for a todo app in Python, but I'm not sure where to start

[Ralph provides architecture overview and asks clarifying questions]

> Let's use FastAPI. Create the basic project structure

[Ralph creates files, explains choices, shows next steps]

> Now add a User model with authentication

[Ralph implements user model, database setup, auth middleware]

> The password hashing isn't working. Here's the error...

[Ralph debugs, fixes the issue, explains the problem]

> Add rate limiting to prevent abuse

[Ralph implements rate limiting, adds configuration, updates docs]

> Write comprehensive tests for everything we've built

[Ralph creates test suite, explains testing strategy]

> How do I deploy this to production?

[Ralph provides deployment guide, Docker setup, environment config]
```

**This is how development should feel**: Natural conversation that turns ideas into production-ready code through collaborative iteration. No context switching, no losing your train of thought, no starting over when you get stuck.

## The Force Multiplier Effect

### 🎯 **From Concept to Code in Minutes**
Stop wrestling with APIs, fighting documentation, or translating pseudocode. Ralph takes your high-level ideas and transforms them into working implementations faster than you can say "Stack Overflow."

### 🧬 **Code Evolution on Demand**
Ralph doesn't just write code - it evolves it. Refactor monoliths into microservices, migrate between frameworks, optimize algorithms, or completely rewrite systems in different languages while preserving business logic.

### 🎓 **Learn While You Build**
Every interaction with Ralph is a masterclass. It explains its reasoning, teaches best practices, and helps you understand not just what to code, but why. You'll absorb years of experience in weeks.

### 🔄 **Conversational Context That Never Breaks**
Ralph remembers everything from your session. Built that authentication system two hours ago? Ralph knows exactly how it works when you want to add features. Ran into an error? Ralph remembers the solution when similar issues come up. It's like pair programming with someone who has perfect memory and infinite patience.

### ⚡ **Scriptable Superpowers**
One-shot mode means Ralph becomes part of your development infrastructure:
- **Pre-commit hooks** that refactor code automatically
- **CI/CD pipelines** that fix failing tests
- **Automated code reviews** that catch issues before humans even look
- **Documentation generators** that actually understand your code
- **AGENT.md support** - Define custom AI behavior with the [agent.md](https://agent.md/) specification

## Universal Compatibility

**One Binary. Every Platform. Zero Compromises.**
- Linux, Windows, macOS, FreeBSD, NetBSD, OpenBSD
- No installation, no dependencies, no containers
- Built with [Cosmopolitan Libc](https://cosmos.zip) for true portability

## Get Started in 30 Seconds

### Option 1: Download and Go
```bash
# Download the latest release
wget https://github.com/bluetongueai/ralph/releases/latest/download/ralph
chmod +x ralph

# Set your API key
export OPENAI_API_KEY=sk-your-key-here
# or
export ANTHROPIC_API_KEY=sk-ant-your-key-here

# Start building the future
./ralph "Let's build something amazing"
```

### Option 2: Build from Source
```bash
git clone https://github.com/bluetongueai/ralph
cd ralph
make
# Coffee break while it builds...
./ralph "Hello, world"
```

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
| `CONTEXT_WINDOW` | Model context window size | `8192` |
| `MAX_CONTEXT_WINDOW` | Maximum context window allowed | Same as `CONTEXT_WINDOW` |
| `MAX_TOKENS` | Max response tokens | Auto-calculated |
| `OPENAI_API_KEY` | OpenAI API key | None |
| `ANTHROPIC_API_KEY` | Anthropic API key | None |

### Advanced Token Management

Ralph features intelligent token management that ensures models always have enough context for meaningful responses:

- **Dynamic Context Window Usage**: Automatically uses `MAX_CONTEXT_WINDOW` when the conversation exceeds `CONTEXT_WINDOW`
- **Intelligent Safety Buffers**: Calculates safety buffers dynamically based on context complexity (5% of context window + base buffer)
- **Conversation History Trimming**: Automatically trims older messages when context limits are reached, preserving recent tool interactions
- **Minimum Response Guarantee**: Always reserves at least 150 tokens for model responses
- **Accurate Token Estimation**: Uses improved token counting (3.5 chars/token) with overhead for JSON structure and tools

The system prioritizes response quality by maximizing available response tokens while ensuring the prompt fits within context limits.

## Real-World Usage Examples

### 🎯 **The "I Have No Idea What I'm Doing" Scenario**
```bash
# You need to build something you've never built before
./ralph "I need to create a real-time chat app with WebSockets, but I've never used WebSockets. Walk me through building it from scratch in Node.js"

# Result: Complete tutorial + working code + deployment instructions
```

### 🔍 **The "Legacy Code Nightmare" Scenario**
```bash
# That 1000-line function nobody wants to touch
./ralph "This function is a monster. Break it down, explain what each part does, and refactor it into clean, testable modules" < legacy_monster.py

# Result: Detailed analysis + refactored code + migration strategy
```

### 🚀 **The "Ship It Yesterday" Scenario**
```bash
# When deadlines are breathing down your neck
./ralph "I need a complete REST API for a blog platform with authentication, CRUD operations, and pagination. Make it production-ready with proper error handling and validation."

# Result: Full implementation + tests + documentation
```

### 🎓 **The "Learn While Building" Scenario**
```bash
$ ./ralph
> I want to learn Rust by building a command-line tool that parses CSV files

[Ralph: "Great choice! Let me walk you through setting up a new Rust project..." 
Creates Cargo.toml, explains project structure, builds basic CSV parser]

> This is working! Now add multi-threading for large files

[Ralph: "Perfect timing to learn about Rust's concurrency model..."
Implements thread pool, explains ownership rules, shows performance comparison]

> Add proper error handling with the ? operator

[Ralph: "The ? operator is one of Rust's best features for error handling..."
Refactors code with Result types, explains error propagation, adds custom errors]

> Show me how to write comprehensive unit tests

[Ralph: "Testing in Rust is fantastic - let me show you the conventions..."
Creates test module, explains #[cfg(test)], implements property-based testing]

> How do I package this for distribution?

[Ralph: "Let's get this ready for crates.io..."
Sets up GitHub Actions, configures publishing, explains semantic versioning]

# Result: Not just working code, but deep understanding of Rust ecosystem
```

**The Learning Accelerator**: Ralph doesn't just give you fish - it teaches you to fish while helping you build an entire fishing industry.

## Automation and Integration

### 🔄 **Git Workflow Automation**
```bash
# .git/hooks/pre-commit
#!/bin/bash
./ralph "Review this commit for potential issues and suggest improvements" < <(git diff --cached)

# .git/hooks/prepare-commit-msg  
#!/bin/bash
commit_msg=$(./ralph "Generate a commit message for: $(git diff --staged --name-only | tr '\n' ' ')")
echo "$commit_msg" > $1
```

### 🏗️ **CI/CD Integration**
```yaml
# GitHub Actions
- name: AI Code Review
  run: |
    git diff origin/main..HEAD | ./ralph "Provide a thorough code review with specific suggestions"

# GitLab CI
script:
  - ./ralph "Analyze test failures and suggest fixes" < test_results.log
```

### 🤖 **Development Workflow Automation**
```bash
# Build error assistant
make 2>&1 | ./ralph "Fix these build errors and explain what went wrong"

# Automated documentation
./ralph "Generate API documentation for all functions in src/" 

# Test failure debugging
pytest --tb=short 2>&1 | ./ralph "Debug these test failures and provide fixes"
```

### 🎛️ **Custom AI Behavior**
Create an `AGENT.md` file following the [agent.md specification](https://agent.md/):

```markdown
# Development Assistant Agent

You are a senior software architect specializing in distributed systems.
Focus on scalability, reliability, and maintainability.

## Priorities
- Performance optimization
- Security best practices  
- Clean architecture principles
```

## Why Ralph Changes Everything

### For Individual Developers
Ralph transforms you from someone who googles "how to" into someone who just *does*. No more context switching between Stack Overflow tabs, no more deciphering cryptic error messages alone, no more getting stuck on APIs you've never used. Ralph is your 24/7 senior developer who never gets impatient with your questions.

### For Teams
Ralph democratizes expertise across your entire team. Junior developers can tackle senior-level problems with Ralph's guidance. Senior developers can focus on architecture while Ralph handles the implementation details. Code reviews become collaborative learning sessions instead of gatekeeping exercises.

### For Companies
Ralph is the ultimate force multiplier for development velocity. It reduces onboarding time from months to weeks, turns every developer into a polyglot programmer, and ensures knowledge isn't locked in individual heads. When your entire team has access to expert-level AI assistance, innovation accelerates exponentially.

## Universal Deployment

**Runs everywhere. Breaks nowhere.**
- Linux, Windows, macOS, FreeBSD, NetBSD, OpenBSD
- Servers, desktops, containers, CI/CD pipelines
- No runtime dependencies, no installation headaches
- One binary to rule them all

## Get Ralph Now

```bash
# The future of programming is one download away
wget https://github.com/bluetongueai/ralph/releases/latest/download/ralph
chmod +x ralph
export OPENAI_API_KEY=your-key-here
./ralph "Transform me into a 10x developer"
```

---

**Ralph: Because every programmer deserves a senior developer in their corner.**

Built with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) for true universal portability.
