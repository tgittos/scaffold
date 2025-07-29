# Hello World

A simple "Hello, World!" program written in C and built with Cosmopolitan Libc.

## Building

This project uses [Cosmopolitan Libc](https://cosmos.zip) for cross-platform compilation.

### Prerequisites

- [Cosmopolitan Libc](https://cosmos.zip) compiler toolchain
- GNU Autotools (autoconf, automake, autotools-dev, m4)

### Quick Build

The simplest way to build:

```bash
cosmocc src/main.c -o hello-world
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
- `hello-world` - Main executable
- `hello-world.aarch64.elf` - Native ARM64 executable
- `hello-world.com.dbg` - Debug symbols

## Running

```bash
./hello-world
```

Output:
```
Hello, World!
```

## Development Container

This project includes a devcontainer configuration that sets up:
- Debian bookworm-slim base
- Cosmopolitan Libc toolchain
- GNU Autotools
- Node.js and Claude Code CLI