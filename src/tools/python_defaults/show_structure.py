"""Analyze code structure without reading full files.

Gate: file_read
Match: path
"""

import os
import re

MAX_DEPTH = 5
MAX_FILES = 200
MAX_SYMBOLS_PER_FILE = 200

# Skip directories that are never interesting
SKIP_DIRS = {
    ".git", ".hg", ".svn", "node_modules", "__pycache__", ".tox", ".mypy_cache",
    ".pytest_cache", "venv", ".venv", "env", ".env", "dist", "build", ".eggs",
    "egg-info", ".idea", ".vscode", "target", "vendor",
}

SKIP_EXTENSIONS = {
    ".pyc", ".pyo", ".so", ".o", ".a", ".dylib", ".dll", ".exe", ".class",
    ".jar", ".whl", ".egg", ".tar", ".gz", ".zip", ".png", ".jpg", ".gif",
    ".ico", ".svg", ".woff", ".ttf", ".eot", ".pdf", ".lock",
}

LANG_MAP = {
    ".py": "python",
    ".js": "javascript", ".jsx": "javascript", ".mjs": "javascript",
    ".ts": "typescript", ".tsx": "typescript",
    ".rs": "rust",
    ".go": "go",
    ".java": "java",
    ".c": "c", ".h": "c",
    ".cpp": "cpp", ".cc": "cpp", ".cxx": "cpp", ".hpp": "cpp",
    ".rb": "ruby",
    ".swift": "swift",
    ".kt": "kotlin", ".kts": "kotlin",
    ".scala": "scala",
    ".cs": "csharp",
}

# Regex patterns per language for structural extraction
PATTERNS = {
    "python": {
        "class": re.compile(r"^class\s+(\w+)(?:\((.*?)\))?:", re.MULTILINE),
        "function": re.compile(r"^(?:async\s+)?def\s+(\w+)\s*\(", re.MULTILINE),
        "method": re.compile(r"^(\s+)(?:async\s+)?def\s+(\w+)\s*\(", re.MULTILINE),
        "import_from": re.compile(r"^from\s+(\S+)\s+import\s+(.+)", re.MULTILINE),
        "import": re.compile(r"^import\s+(\S+)", re.MULTILINE),
        "constant": re.compile(r"^([A-Z][A-Z_0-9]{2,})\s*=", re.MULTILINE),
    },
    "javascript": {
        "class": re.compile(r"^(?:export\s+)?(?:default\s+)?class\s+(\w+)(?:\s+extends\s+(\w+))?", re.MULTILINE),
        "function": re.compile(r"^(?:export\s+)?(?:async\s+)?function\s+(\w+)", re.MULTILINE),
        "arrow": re.compile(r"^(?:export\s+)?(?:const|let|var)\s+(\w+)\s*=\s*(?:async\s+)?(?:\([^)]*\)|[^=])\s*=>", re.MULTILINE),
        "import": re.compile(r"^import\s+(?:\{([^}]+)\}|(\w+)|\*\s+as\s+(\w+))\s+from\s+['\"]([^'\"]+)", re.MULTILINE),
        "require": re.compile(r"(?:const|let|var)\s+(?:\{([^}]+)\}|(\w+))\s*=\s*require\(['\"]([^'\"]+)", re.MULTILINE),
    },
    "typescript": None,  # Shares javascript patterns, set below
    "rust": {
        "struct": re.compile(r"^(?:pub(?:\(crate\))?\s+)?struct\s+(\w+)", re.MULTILINE),
        "enum": re.compile(r"^(?:pub(?:\(crate\))?\s+)?enum\s+(\w+)", re.MULTILINE),
        "function": re.compile(r"^(?:pub(?:\(crate\))?\s+)?(?:async\s+)?fn\s+(\w+)", re.MULTILINE),
        "impl": re.compile(r"^impl(?:<[^>]*>)?\s+(?:(\w+)\s+for\s+)?(\w+)", re.MULTILINE),
        "trait": re.compile(r"^(?:pub(?:\(crate\))?\s+)?trait\s+(\w+)", re.MULTILINE),
        "use": re.compile(r"^use\s+(.+);", re.MULTILINE),
        "mod": re.compile(r"^(?:pub\s+)?mod\s+(\w+)", re.MULTILINE),
    },
    "go": {
        "struct": re.compile(r"^type\s+(\w+)\s+struct\b", re.MULTILINE),
        "interface": re.compile(r"^type\s+(\w+)\s+interface\b", re.MULTILINE),
        "function": re.compile(r"^func\s+(?:\(\w+\s+\*?(\w+)\)\s+)?(\w+)\s*\(", re.MULTILINE),
        "import": re.compile(r'"([^"]+)"'),
    },
    "java": {
        "class": re.compile(r"^(?:public\s+)?(?:abstract\s+)?class\s+(\w+)(?:\s+extends\s+(\w+))?", re.MULTILINE),
        "interface": re.compile(r"^(?:public\s+)?interface\s+(\w+)", re.MULTILINE),
        "method": re.compile(r"^\s+(?:public|private|protected)?\s*(?:static\s+)?(?:\w+(?:<[^>]*>)?)\s+(\w+)\s*\(", re.MULTILINE),
        "import": re.compile(r"^import\s+(.+);", re.MULTILINE),
    },
    "c": {
        "function": re.compile(r"^(?:static\s+)?(?:inline\s+)?(?:const\s+)?(?:unsigned\s+)?(?:struct\s+)?\w+[\s*]+(\w+)\s*\([^)]*\)\s*\{", re.MULTILINE),
        "struct": re.compile(r"^(?:typedef\s+)?struct\s+(\w+)", re.MULTILINE),
        "typedef": re.compile(r"^typedef\s+.*?(\w+)\s*;", re.MULTILINE),
        "define": re.compile(r"^#define\s+(\w+)", re.MULTILINE),
        "include": re.compile(r'^#include\s+[<"]([^>"]+)[>"]', re.MULTILINE),
    },
    "cpp": None,  # Set below
    "ruby": {
        "class": re.compile(r"^class\s+(\w+)(?:\s*<\s*(\w+))?", re.MULTILINE),
        "module": re.compile(r"^module\s+(\w+)", re.MULTILINE),
        "method": re.compile(r"^\s*def\s+(\w+)", re.MULTILINE),
        "require": re.compile(r"^require\s+['\"]([^'\"]+)", re.MULTILINE),
    },
}

# Shared patterns
PATTERNS["typescript"] = PATTERNS["javascript"]
PATTERNS["cpp"] = PATTERNS["c"]


def _detect_language(filepath):
    ext = os.path.splitext(filepath)[1].lower()
    return LANG_MAP.get(ext)


def _collect_files(path, depth, max_files):
    """Collect source files up to a given depth."""
    files = []
    path = os.path.abspath(path)
    for root, dirs, filenames in os.walk(path):
        rel_root = os.path.relpath(root, path)
        if rel_root == ".":
            current_depth = 0
        else:
            current_depth = rel_root.count(os.sep) + 1
        if current_depth >= depth:
            dirs.clear()
            continue
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith(".")]
        dirs.sort()
        for fn in sorted(filenames):
            ext = os.path.splitext(fn)[1].lower()
            if ext in SKIP_EXTENSIONS:
                continue
            lang = LANG_MAP.get(ext)
            if lang is None:
                continue
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, path)
            files.append((rel, full, lang))
            if len(files) >= max_files:
                return files
    return files


def _extract_symbols(content, lang):
    """Extract symbols from file content using language patterns."""
    patterns = PATTERNS.get(lang)
    if not patterns:
        return []

    symbols = []
    lines = content.split("\n")

    # Build line number index
    line_offsets = [0]
    for line in lines:
        line_offsets.append(line_offsets[-1] + len(line) + 1)

    def _offset_to_line(offset):
        for i, lo in enumerate(line_offsets):
            if lo > offset:
                return i
        return len(lines)

    # Track current class for method association (Python/Java/Ruby)
    current_class = None
    current_class_indent = -1

    for kind, regex in patterns.items():
        for m in regex.finditer(content):
            line_num = _offset_to_line(m.start())

            if kind == "class":
                name = m.group(1)
                bases = m.group(2) if m.lastindex >= 2 else None
                sym = {"type": "class", "name": name, "line": line_num}
                if bases:
                    sym["bases"] = [b.strip() for b in bases.split(",")]
                symbols.append(sym)

            elif kind == "function":
                name = m.group(m.lastindex) if m.lastindex else m.group(1)
                symbols.append({"type": "function", "name": name, "line": line_num})

            elif kind == "method":
                if lang == "python":
                    indent = len(m.group(1))
                    name = m.group(2)
                    symbols.append({"type": "method", "name": name, "line": line_num})
                else:
                    name = m.group(1)
                    symbols.append({"type": "method", "name": name, "line": line_num})

            elif kind == "arrow":
                name = m.group(1)
                symbols.append({"type": "function", "name": name, "line": line_num})

            elif kind in ("struct", "enum", "trait", "interface", "module", "typedef"):
                name = m.group(1)
                symbols.append({"type": kind, "name": name, "line": line_num})

            elif kind == "impl":
                trait = m.group(1) if m.lastindex >= 2 and m.group(1) else None
                target = m.group(2) if m.lastindex >= 2 else m.group(1)
                sym = {"type": "impl", "name": target, "line": line_num}
                if trait:
                    sym["trait"] = trait
                symbols.append(sym)

            elif kind == "constant" or kind == "define":
                name = m.group(1)
                symbols.append({"type": "constant", "name": name, "line": line_num})

            elif kind == "mod":
                name = m.group(1)
                symbols.append({"type": "module", "name": name, "line": line_num})

            if len(symbols) >= MAX_SYMBOLS_PER_FILE:
                break

    # Sort by line number
    symbols.sort(key=lambda s: s["line"])
    return symbols


def _extract_imports(content, lang):
    """Extract import statements from file content."""
    patterns = PATTERNS.get(lang)
    if not patterns:
        return []
    imports = []
    for kind in ("import", "import_from", "use", "include", "require"):
        regex = patterns.get(kind)
        if not regex:
            continue
        for m in regex.finditer(content):
            if kind == "import_from":
                imports.append(m.group(1))
            elif kind == "import":
                if lang in ("javascript", "typescript"):
                    # Group 4 is the module path
                    imports.append(m.group(m.lastindex))
                else:
                    imports.append(m.group(1))
            elif kind == "require":
                imports.append(m.group(m.lastindex))
            else:
                imports.append(m.group(1))
    return sorted(set(imports))


def _read_file_safe(filepath, max_size=1024 * 1024):
    try:
        if os.path.getsize(filepath) > max_size:
            return None
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except (OSError, IOError):
        return None


def _op_outline(path, depth, language_filter):
    path = os.path.abspath(path)
    if os.path.isfile(path):
        lang = language_filter or _detect_language(path)
        if not lang:
            return {"error": f"Cannot detect language for {path}"}
        content = _read_file_safe(path)
        if content is None:
            return {"error": f"Cannot read file: {path}"}
        symbols = _extract_symbols(content, lang)
        return {
            "operation": "outline",
            "path": path,
            "language": lang,
            "symbols": symbols,
            "summary": f"{len(symbols)} symbols",
        }

    files = _collect_files(path, depth, MAX_FILES)
    modules = []
    total_symbols = 0
    type_counts = {}
    for rel, full, lang in files:
        if language_filter and lang != language_filter:
            continue
        content = _read_file_safe(full)
        if content is None:
            continue
        symbols = _extract_symbols(content, lang)
        if symbols:
            modules.append({
                "file": rel,
                "language": lang,
                "symbols": symbols,
            })
            total_symbols += len(symbols)
            for s in symbols:
                type_counts[s["type"]] = type_counts.get(s["type"], 0) + 1

    summary_parts = [f"{len(modules)} files", f"{total_symbols} symbols"]
    for t in ("class", "function", "method", "struct", "trait", "interface"):
        if t in type_counts:
            summary_parts.append(f"{type_counts[t]} {t}{'s' if type_counts[t] != 1 else ''}")

    return {
        "operation": "outline",
        "path": path,
        "modules": modules,
        "summary": ", ".join(summary_parts),
        "truncated": len(files) >= MAX_FILES,
    }


def _op_imports(path, depth, language_filter):
    path = os.path.abspath(path)
    if os.path.isfile(path):
        lang = language_filter or _detect_language(path)
        if not lang:
            return {"error": f"Cannot detect language for {path}"}
        content = _read_file_safe(path)
        if content is None:
            return {"error": f"Cannot read file: {path}"}
        imports = _extract_imports(content, lang)
        return {
            "operation": "imports",
            "path": path,
            "language": lang,
            "imports": imports,
        }

    files = _collect_files(path, depth, MAX_FILES)
    # Build mapping of module names to relative paths for internal resolution
    module_names = {}
    for rel, full, lang in files:
        base = os.path.splitext(rel)[0].replace(os.sep, ".")
        module_names[base] = rel
        # Also index by just the filename stem
        stem = os.path.splitext(os.path.basename(rel))[0]
        module_names[stem] = rel

    graph = {}
    external = set()
    internal_edges = 0
    for rel, full, lang in files:
        if language_filter and lang != language_filter:
            continue
        content = _read_file_safe(full)
        if content is None:
            continue
        imports = _extract_imports(content, lang)
        deps = []
        for imp in imports:
            # Normalize: "from foo.bar import baz" -> "foo.bar"
            normalized = imp.replace("/", ".").lstrip(".")
            # Check if internal
            is_internal = False
            for mod_name, mod_rel in module_names.items():
                if normalized == mod_name or normalized.startswith(mod_name + "."):
                    if mod_rel != rel:
                        deps.append(mod_rel)
                        internal_edges += 1
                        is_internal = True
                    break
            if not is_internal:
                external.add(imp)
        if deps:
            graph[rel] = sorted(set(deps))

    return {
        "operation": "imports",
        "path": path,
        "graph": graph,
        "external": sorted(external),
        "internal_edges": internal_edges,
        "summary": f"{len(files)} modules, {internal_edges} internal deps, {len(external)} external deps",
    }


def _op_hierarchy(path, depth, language_filter, symbol):
    path = os.path.abspath(path)
    if os.path.isfile(path):
        files = [(os.path.basename(path), path, language_filter or _detect_language(path))]
    else:
        files = _collect_files(path, depth, MAX_FILES)

    # Collect all classes with their bases
    classes = {}
    for rel, full, lang in files:
        if language_filter and lang != language_filter:
            continue
        content = _read_file_safe(full)
        if content is None:
            continue
        symbols = _extract_symbols(content, lang)
        for s in symbols:
            if s["type"] in ("class", "struct") and "bases" in s:
                classes[s["name"]] = {
                    "file": rel,
                    "line": s["line"],
                    "bases": s["bases"],
                }
            elif s["type"] in ("class", "struct"):
                classes[s["name"]] = {
                    "file": rel,
                    "line": s["line"],
                    "bases": [],
                }

    # Build children map
    children = {}
    for name, info in classes.items():
        for base in info["bases"]:
            children.setdefault(base, []).append(name)

    # If symbol specified, trace its chain
    if symbol:
        chain = []
        current = symbol
        while current in classes:
            info = classes[current]
            chain.append({"name": current, "file": info["file"],
                         "line": info["line"], "bases": info["bases"]})
            if info["bases"]:
                current = info["bases"][0]  # Follow first base
            else:
                break
        return {
            "operation": "hierarchy",
            "symbol": symbol,
            "chain": chain,
            "children": children.get(symbol, []),
        }

    # Return full hierarchy
    roots = [name for name, info in classes.items()
             if not info["bases"] or not any(b in classes for b in info["bases"])]
    return {
        "operation": "hierarchy",
        "classes": classes,
        "roots": sorted(roots),
        "children": children,
        "summary": f"{len(classes)} classes, {len(roots)} root{'s' if len(roots) != 1 else ''}",
    }


def _op_exports(path, depth, language_filter):
    path = os.path.abspath(path)
    if os.path.isfile(path):
        files = [(os.path.basename(path), path, language_filter or _detect_language(path))]
    else:
        files = _collect_files(path, depth, MAX_FILES)

    modules = []
    for rel, full, lang in files:
        if language_filter and lang != language_filter:
            continue
        content = _read_file_safe(full)
        if content is None:
            continue
        symbols = _extract_symbols(content, lang)
        # Filter to public symbols
        public = []
        for s in symbols:
            name = s["name"]
            if lang == "python":
                if s["type"] == "method":
                    continue  # Methods aren't module-level exports
                if name.startswith("_"):
                    continue
            elif lang in ("rust",):
                pass  # Rust patterns already filter on pub
            elif lang == "go":
                if not name[0].isupper():
                    continue
            public.append(s)
        if public:
            modules.append({"file": rel, "exports": public})

    total = sum(len(m["exports"]) for m in modules)
    return {
        "operation": "exports",
        "path": path,
        "modules": modules,
        "summary": f"{len(modules)} files, {total} public symbols",
    }


def show_structure(path: str, operation: str = "outline", depth: int = 2,
                   language: str = None, symbol: str = None) -> dict:
    """Get an overview of code structure without reading full file contents. Use this before read_file when you need to understand a module's shape — what classes exist, what methods they have, how files relate. Use 'outline' to see all classes, functions, and methods with line numbers. Use 'imports' to map dependencies between modules. Use 'hierarchy' for class inheritance. Use 'exports' for public API surface. Returns structured data, not source code — much cheaper than reading entire files.

    Args:
        path: File or directory to analyze
        operation: One of 'outline', 'imports', 'hierarchy', 'exports' (default: outline)
        depth: Directory traversal depth (default: 2, max: 5)
        language: Override language detection (e.g., 'python', 'javascript', 'rust', 'go', 'java', 'c')
        symbol: Focus on a specific class/type for hierarchy operation

    Returns:
        Dictionary with operation results, structure data, and summary
    """
    if not os.path.exists(path):
        return {"error": f"Path does not exist: {path}"}

    depth = min(depth, MAX_DEPTH)

    if operation == "outline":
        return _op_outline(path, depth, language)
    elif operation == "imports":
        return _op_imports(path, depth, language)
    elif operation == "hierarchy":
        return _op_hierarchy(path, depth, language, symbol)
    elif operation == "exports":
        return _op_exports(path, depth, language)
    else:
        return {"error": f"Unknown operation: {operation}. Use outline, imports, hierarchy, or exports"}
