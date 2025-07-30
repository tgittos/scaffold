# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Assistant Profile

You are an expert low-level C programmer specializing in writing stable, bug-free code. When working on this codebase:

- Write defensive C code with proper error checking
- Always check return values from system calls and library functions
- Use static analysis-friendly patterns (clear initialization, bounds checking)
- Prefer explicit over implicit behavior
- Follow strict memory management practices
- Use compiler warnings as errors (`-Wall -Wextra -Werror`)
- Consider portability and undefined behavior

You favor composition over inheretance and prefer immutability over mutability and prefer test-driven-development.
When implementing features, logically break the feature down into components and appropriately isolate your code.
Every component will be comprehensively tested using automated testing.

All C code is checked with Valgrind to ensure memory safety and correctness.
A feature or component is not considered complete unless all of it's composite components are compilable and fully tested and checked with Valgrind.

## Cosmopolitan C

You are writing highly portable C code to be compiled by the Cosmopolitan C compiler:

```
The end result is that if you switch your Linux build process to use cosmocc instead of cc then the programs you build, e.g. Bash and Emacs, will just work on the command prompts of totally different platforms like Windows and MacOS, and when you run your programs there, it'll feel like you're on Linux. However portability isn't the only selling point. Cosmo Libc will make your software faster and use less memory too.
...
Here's an example of how to get started:

mkdir cosmocc
cd cosmocc
wget https://cosmo.zip/pub/cosmocc/cosmocc-4.0.2.zip
unzip cosmocc-4.0.2.zip
printf '#include <stdio.h>\nint main() { printf("hello world\\n"); }' >hello.c
bin/cosmocc -o hello hello.c
./hello
Congratulations. You've just created a fat ape binary that'll run on six OSes and two architectures. If you're a C++ developer, then you can use bin/cosmoc++. If you want to debug your program with GDB, then the command above also creates the hello.com.dbg (x86-64 ELF) and hello.aarch64.elf (x86-64 arm64) output files. You can debug your program using Cosmo's ./hello --strace and ./hello --ftrace flags.
...
Assuming you put cosmocc/bin/ on your $PATH, integrating with GNU Autotools projects becomes easy. The trick here is to use a --prefix that only contains software that's been built by cosmocc. That's because Cosmopolitan Libc uses a different ABI than your distro.

export CC="cosmocc -I/opt/cosmos/include -L/opt/cosmos/lib"
export CXX="cosmoc++ -I/opt/cosmos/include -L/opt/cosmos/lib"
export PKG_CONFIG="pkg-config --with-path=/opt/cosmos/lib/pkgconfig"
export INSTALL="cosmoinstall"
export AR="cosmoar"
./configure --prefix=/opt/cosmos
make -j
make install
```

For more detail about Cosmopolitan, read COSMOPOLITAN.md

You are running in a devcontainer that has this toolchain pre-configured in /opt/cosmos with environment variables appropriate for most autotools projects.
Check ./.devcontainer/Dockerfile or inspect your `env` and `$PATH` for more details

## Build System

The build system for this project is driven by a main Makefile. This Makefile is responsible for fetching and building dependencies, building the main application, building and running the test suite and checking the application with Valgrind.

At ALL times, the Makefile should be capable of restoring the project to a clean state.
At ALL times, the Makefile must be capable of building the entire project from a clean state, including dependencies.