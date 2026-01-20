# Cosmopolitan Python Build

Builds portable Python using Cosmopolitan Libc. Creates a fat binary that runs on Linux, macOS, Windows, and BSDs.

## Dependencies

- [git](https://git-scm.com)
- [python](https://www.python.org) - `python3.12`
- [uv](https://docs.astral.sh/uv) - `curl -LsSf https://astral.sh/uv/install.sh | sh`
- Cosmopolitan toolchain (cosmocc, cosmoc++, etc.)

## Shared Dependencies with Ralph

This build shares dependencies with the main ralph project to avoid rebuilding:

| Dependency | Version | Path |
|------------|---------|------|
| zlib | 1.3.1 | `../deps/zlib-1.3.1/` |
| readline | 8.2 | `../deps/readline-8.2/` (optional) |
| ncurses | 6.4 | `../deps/ncurses-6.4/` (optional) |

If these dependencies exist (built by main `make`), they will be used automatically.

## Build

```bash
# From python/ directory
make

# Or to ensure deps are built first, from project root
make deps/zlib-1.3.1/libz.a
cd python && make
```

## Build Outputs

| Output | Location | Description |
|--------|----------|-------------|
| `python.com` | `python/python.com` | Portable Python fat binary |
| `libpython3.12.a` | `build/libpython3.12.a` | Static library for linking with ralph |
| Python headers | `build/python-include/` | Headers for C/C++ embedding |

## Linking with Ralph

After building Python, ralph can link with the static library:

```makefile
PYTHON_LIB = $(BUILDDIR)/libpython3.12.a
PYTHON_INCLUDES = -I$(BUILDDIR)/python-include
```

## Clean

```bash
make clean      # Remove build artifacts, keep downloads
make distclean  # Remove everything including downloads
```
