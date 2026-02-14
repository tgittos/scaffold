#!/usr/bin/env python3
"""Generate a single flat header for libagent.

Recursively resolves #include directives starting from lib/libagent.h,
expanding them inline (like the C preprocessor). Vendored dependency
headers are inlined; standard C/POSIX headers are left as-is.

Uses compare-and-swap: only writes when content changes.

Usage: gen_header.py <output-file>
"""

import re
import sys
from pathlib import Path

# Headers provided by the system toolchain — never inlined
SYSTEM_HEADERS = {
    # C standard
    "assert.h", "complex.h", "ctype.h", "errno.h", "fenv.h", "float.h",
    "inttypes.h", "iso646.h", "limits.h", "locale.h", "math.h",
    "setjmp.h", "signal.h", "stdalign.h", "stdarg.h", "stdatomic.h",
    "stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h",
    "stdnoreturn.h", "string.h", "tgmath.h", "threads.h", "time.h",
    "uchar.h", "wchar.h", "wctype.h",
    # POSIX / Unix
    "aio.h", "arpa/inet.h", "dirent.h", "dlfcn.h", "fcntl.h",
    "fnmatch.h", "ftw.h", "glob.h", "grp.h", "iconv.h",
    "langinfo.h", "libgen.h", "monetary.h", "net/if.h", "netdb.h",
    "netinet/in.h", "netinet/tcp.h", "poll.h", "pthread.h", "pwd.h",
    "regex.h", "sched.h", "search.h", "semaphore.h", "spawn.h",
    "strings.h", "sys/ioctl.h", "sys/ipc.h", "sys/mman.h",
    "sys/resource.h", "sys/select.h", "sys/socket.h", "sys/stat.h",
    "sys/statvfs.h", "sys/time.h", "sys/times.h", "sys/types.h",
    "sys/uio.h", "sys/un.h", "sys/utsname.h", "sys/wait.h",
    "syslog.h", "termios.h", "unistd.h", "utime.h",
    # Windows
    "windows.h", "winsock2.h", "ws2tcpip.h",
}

INCLUDE_RE = re.compile(r'\s*#\s*include\s+([<"])([^>"]+)[>"]')


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <output-file>", file=sys.stderr)
        sys.exit(1)

    root = Path(__file__).resolve().parent.parent
    outfile = Path(sys.argv[1])
    if not outfile.is_absolute():
        outfile = root / outfile

    # Build include search paths from dep directories
    search_paths = [root, root / "lib"]
    for d in sorted((root / "deps").iterdir()):
        if not d.is_dir():
            continue
        inc = d / "include"
        if inc.is_dir():
            search_paths.append(inc)
        search_paths.append(d)
    search_paths.append(root / "build" / "generated")
    search_paths.append(root / "build")

    # libagent.h is an umbrella with forward declarations that conflict
    # with the full definitions it includes — expand its includes but
    # skip its own non-include content.
    entry = (root / "lib" / "libagent.h").resolve()
    skip_emit = {entry}

    visited: set[Path] = set()

    def resolve(inc_name: str, from_file: Path, is_quoted: bool) -> Path | None:
        if is_quoted:
            candidate = (from_file.parent / inc_name).resolve()
            if candidate.is_file():
                return candidate
        for sp in search_paths:
            candidate = (sp / inc_name).resolve()
            if candidate.is_file():
                return candidate
        return None

    def rel(filepath: Path) -> str:
        try:
            return str(filepath.relative_to(root))
        except ValueError:
            return str(filepath)

    def expand(filepath: Path) -> list[str]:
        """Expand a header inline, recursing into #includes at their position."""
        filepath = filepath.resolve()
        if filepath in visited:
            return []
        visited.add(filepath)

        emit = filepath not in skip_emit
        result: list[str] = []
        if emit:
            result.append(f"/* --- {rel(filepath)} --- */")

        for line in filepath.read_text().splitlines():
            m = INCLUDE_RE.match(line)
            if not m:
                if emit:
                    result.append(line)
                continue

            bracket, inc_name = m.group(1), m.group(2)
            is_quoted = bracket == '"'

            if inc_name in SYSTEM_HEADERS:
                if emit:
                    result.append(line)
                continue

            resolved = resolve(inc_name, filepath, is_quoted)
            if resolved:
                result.extend(expand(resolved))
            elif str(filepath).startswith(str(root / "lib")):
                print(f"error: cannot resolve #include {bracket}{inc_name}{'>' if bracket == '<' else '\"'} "
                      f"from {rel(filepath)}", file=sys.stderr)
                sys.exit(1)
            elif emit:
                result.append(line)

        if emit:
            result.append("")
        return result

    # Phase 1: expand from the umbrella header (canonical ordering)
    body = expand(entry)

    # Phase 2: sweep remaining lib/ headers not reached from the umbrella
    lib_headers = sorted(
        h.resolve()
        for h in (root / "lib").rglob("*.h")
        if h.name != "hnswlib_wrapper.h"
    )
    for h in lib_headers:
        body.extend(expand(h))

    # Assemble final output
    out: list[str] = [
        "/*",
        " * libagent.h - Single-file header for libagent",
        " *",
        " * Auto-generated by scripts/gen_header.py — do not edit.",
        " * Include this file and link against libagent.a.",
        " */",
        "",
        "#ifndef LIBAGENT_H",
        "#define LIBAGENT_H",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    out.extend(body)
    out.extend([
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif /* LIBAGENT_H */",
        "",
    ])

    content = "\n".join(out)

    outfile.parent.mkdir(parents=True, exist_ok=True)
    if outfile.exists() and outfile.read_text() == content:
        return
    outfile.write_text(content)


if __name__ == "__main__":
    main()
