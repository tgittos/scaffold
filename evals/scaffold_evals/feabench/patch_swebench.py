"""Patch installed swebench package to support FEA-bench repos.

FEA-bench repos aren't in swebench's MAP_REPO_VERSION_TO_SPECS. This script
adds a DefaultDict fallback and FEA-bench-specific environment specs, mirroring
the changes from https://github.com/microsoft/FEA-Bench/blob/main/swe-bench.diff
but targeting swebench 4.1.0+.

Run after `uv sync --extra feabench`:
    uv run python -m scaffold_evals.feabench.patch_swebench
"""

from __future__ import annotations

import importlib
import site
import sys
from pathlib import Path


def _find_swebench_dir() -> Path:
    """Locate the installed swebench package directory."""
    import swebench
    return Path(swebench.__file__).parent


def _patch_constants_python(swebench_dir: Path) -> None:
    """Add DefaultDict and FEA-bench repo specs to constants/python.py."""
    path = swebench_dir / "harness" / "constants" / "python.py"
    text = path.read_text()

    if "class DefaultDict" in text:
        print(f"  [skip] {path.name} already patched")
        return

    # 1. Add DefaultDict class at the top
    default_dict_class = '''\
class DefaultDict(dict):
    def __getitem__(self, key):
        try:
            return super().__getitem__(key)
        except KeyError:
            return self.get('default', None)

'''
    text = default_dict_class + text

    # 2. Add FEA-bench repo specs before MAP_REPO_VERSION_TO_SPECS_PY
    feabench_specs = '''
SPECS_FEABENCH_PY39 = DefaultDict({
    k: {
        "python": "3.9",
        "install": "pip install -e .",
        "pip_packages": ["pytest"],
        "test_cmd": "pytest --no-header -rA --tb=no -p no:cacheprovider",
    }
    for k in [None, 'default']
})

SPECS_FEABENCH_PY310 = DefaultDict({
    k: {
        "python": "3.10",
        "install": "pip install -e .",
        "pip_packages": ["pytest"],
        "test_cmd": "pytest --no-header -rA --tb=no -p no:cacheprovider",
    }
    for k in [None, 'default']
})

SPECS_FEABENCH_CONAN = DefaultDict({
    k: {
        "python": "3.9",
        "install": "pip install -e .",
        "pip_packages": ["pytest", "bottle", "mock", "webtest", "jwt"],
        "test_cmd": "pytest --no-header -rA --tb=no -p no:cacheprovider",
    }
    for k in [None, 'default', '2.10', '2.12']
})

SPECS_FEABENCH_PYTORCH_VISION = DefaultDict({
    k: {
        "python": "3.9",
        "install": "pip install -e .",
        "pip_packages": ["pytest", "torch"],
        "test_cmd": "pytest --no-header -rA --tb=no -p no:cacheprovider",
    }
    for k in [None, 'default']
})

SPECS_FEABENCH_ACCELERATE = DefaultDict({
    k: {
        "python": "3.9",
        "install": "pip install torch==2.5.0 ; pip install -e .",
        "pip_packages": ["pytest"],
        "test_cmd": "pytest --no-header -rA --tb=no -p no:cacheprovider",
    }
    for k in [None, 'default']
})

SPECS_FEABENCH_TORTOISE = DefaultDict({
    k: {
        "python": "3.9",
        "install": "pip install -e .",
        "pip_packages": ["pydantic", "pytest"],
        "test_cmd": "pytest --no-header -rA --tb=no -p no:cacheprovider",
    }
    for k in [None, 'default']
})

'''
    marker = "MAP_REPO_VERSION_TO_SPECS_PY"
    idx = text.index(marker)
    # Find the line start
    line_start = text.rfind("\n", 0, idx) + 1
    text = text[:line_start] + feabench_specs + text[line_start:]

    # 3. Add FEA-bench repos to MAP_REPO_VERSION_TO_SPECS_PY
    # Find the closing brace of the dict
    map_start = text.index("MAP_REPO_VERSION_TO_SPECS_PY = {")
    # Find the matching closing brace
    brace_depth = 0
    i = text.index("{", map_start)
    while i < len(text):
        if text[i] == "{":
            brace_depth += 1
        elif text[i] == "}":
            brace_depth -= 1
            if brace_depth == 0:
                break
        i += 1
    closing_brace = i

    feabench_repos = '''
    # FEA-bench repos
    "huggingface/datasets": SPECS_FEABENCH_PY310,
    "huggingface/accelerate": SPECS_FEABENCH_ACCELERATE,
    "encode/django-rest-framework": SPECS_FEABENCH_PY310,
    "twisted/twisted": SPECS_FEABENCH_PY310,
    "Cog-Creators/Red-DiscordBot": SPECS_FEABENCH_PY310,
    "conan-io/conan": SPECS_FEABENCH_CONAN,
    "pytorch/vision": SPECS_FEABENCH_PYTORCH_VISION,
    "gradio-app/gradio": SPECS_FEABENCH_PY310,
    "tensorflow/datasets": SPECS_FEABENCH_PY310,
    "tortoise/tortoise-orm": SPECS_FEABENCH_TORTOISE,
    "default": SPECS_FEABENCH_PY39,
'''
    text = text[:closing_brace] + feabench_repos + text[closing_brace:]

    path.write_text(text)
    print(f"  [ok] {path.name}")


def _find_closing_brace(text: str, start: int) -> int:
    """Find the matching closing brace starting from the first { at/after start."""
    i = text.index("{", start)
    depth = 0
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("No matching closing brace found")


def _wrap_dict_with_defaultdict(text: str, var_name: str, extra_entries: str = "") -> str:
    """Replace `VAR = {` with `VAR = DefaultDict({` and close with `})`."""
    target = f"{var_name} = {{"
    if target not in text:
        return text
    start = text.index(target)
    closing = _find_closing_brace(text, start)
    # Insert extra entries before closing brace, then wrap
    text = (
        text[:start] + f"{var_name} = DefaultDict({{"
        + text[start + len(target):closing]
        + extra_entries
        + "})"
        + text[closing + 1:]
    )
    return text


def _patch_constants_init(swebench_dir: Path) -> None:
    """Wrap aggregate maps with DefaultDict."""
    path = swebench_dir / "harness" / "constants" / "__init__.py"
    text = path.read_text()

    if "DefaultDict" in text:
        print(f"  [skip] constants/__init__.py already patched")
        return

    # Import DefaultDict from python module
    text = "from swebench.harness.constants.python import DefaultDict\n" + text

    text = _wrap_dict_with_defaultdict(text, "MAP_REPO_VERSION_TO_SPECS")
    text = _wrap_dict_with_defaultdict(
        text, "MAP_REPO_TO_EXT",
        extra_entries='\n    "default": "py",\n',
    )

    path.write_text(text)
    print(f"  [ok] constants/__init__.py")


def _patch_log_parsers(swebench_dir: Path) -> None:
    """Add default parser fallback."""
    # Patch python.py to add DefaultDict + default parser
    py_path = swebench_dir / "harness" / "log_parsers" / "python.py"
    py_text = py_path.read_text()

    if "class DefaultDict" not in py_text:
        py_text = (
            "class DefaultDict(dict):\n"
            "    def __getitem__(self, key):\n"
            "        try:\n"
            "            return super().__getitem__(key)\n"
            "        except KeyError:\n"
            "            return self.get('default', None)\n\n"
        ) + py_text

    if "MAP_REPO_TO_PARSER_PY = DefaultDict" not in py_text:
        py_text = _wrap_dict_with_defaultdict(
            py_text, "MAP_REPO_TO_PARSER_PY",
            extra_entries='\n    "default": parse_log_pytest_v2,\n',
        )

    py_path.write_text(py_text)
    print(f"  [ok] log_parsers/python.py")

    # Patch __init__.py to use DefaultDict
    init_path = swebench_dir / "harness" / "log_parsers" / "__init__.py"
    init_text = init_path.read_text()

    if "DefaultDict" not in init_text:
        init_text = init_text.replace(
            "from swebench.harness.log_parsers.python import MAP_REPO_TO_PARSER_PY",
            "from swebench.harness.log_parsers.python import MAP_REPO_TO_PARSER_PY, DefaultDict",
        )
        init_text = _wrap_dict_with_defaultdict(init_text, "MAP_REPO_TO_PARSER")

    init_path.write_text(init_text)
    print(f"  [ok] log_parsers/__init__.py")


def _patch_version_lookup(swebench_dir: Path) -> None:
    """Make version lookup in make_test_spec gracefully fall back.

    SWE-bench inner spec dicts are regular dicts keyed by known versions.
    FEA-bench instances may have versions that don't exist in those dicts.
    Replace the raw dict lookup with a safe fallback chain:
      repo_specs[version] -> repo_specs.get(None) -> repo_specs.get('default')
    """
    path = swebench_dir / "harness" / "test_spec" / "test_spec.py"
    text = path.read_text()

    old = "    specs = MAP_REPO_VERSION_TO_SPECS[repo][version]"
    new = (
        "    _repo_specs = MAP_REPO_VERSION_TO_SPECS[repo]\n"
        "    specs = _repo_specs[version] if version in _repo_specs else "
        "(_repo_specs[None] if None in _repo_specs else _repo_specs.get('default', {}))\n"
        "    if not specs:  # FEA-bench: repo in original swebench but version not covered\n"
        "        specs = MAP_REPO_VERSION_TO_SPECS['default'][None]"
    )

    # Fix old broken patches
    old_broken_patterns = [
        # v1: used .get() which bypasses DefaultDict.__getitem__
        (
            "    _repo_specs = MAP_REPO_VERSION_TO_SPECS[repo]\n"
            "    specs = _repo_specs.get(version) or _repo_specs.get(None) or _repo_specs.get('default')"
        ),
        # v2: correct lookup but no fallback for repos in original swebench with unknown versions
        (
            "    _repo_specs = MAP_REPO_VERSION_TO_SPECS[repo]\n"
            "    specs = _repo_specs[version] if version in _repo_specs else "
            "(_repo_specs[None] if None in _repo_specs else _repo_specs.get('default', {}))"
        ),
    ]
    replaced_old = False
    for old_broken in old_broken_patterns:
        if old_broken in text and "if not specs:" not in text:
            text = text.replace(old_broken, old)
            replaced_old = True
            break
    if not replaced_old and "_repo_specs" in text:
        print(f"  [skip] test_spec.py version lookup already patched")
        return

    if old not in text:
        print(f"  [warn] test_spec.py version lookup line not found")
        return

    text = text.replace(old, new)
    path.write_text(text)
    print(f"  [ok] test_spec.py version lookup")


def _patch_version_lookups_globally(swebench_dir: Path) -> None:
    """Replace all direct MAP_REPO_VERSION_TO_SPECS[repo][version] lookups.

    Injects a _safe_specs(repo, version) helper into constants/__init__.py,
    then replaces all direct dict lookups across the swebench codebase.
    """
    # 1. Add helper function to constants/__init__.py
    init_path = swebench_dir / "harness" / "constants" / "__init__.py"
    init_text = init_path.read_text()

    helper = (
        "\n\ndef _safe_specs(repo, version):\n"
        "    \"\"\"Safe version spec lookup with FEA-bench fallback.\"\"\"\n"
        "    _repo_specs = MAP_REPO_VERSION_TO_SPECS[repo]\n"
        "    if version in _repo_specs:\n"
        "        return _repo_specs[version]\n"
        "    if None in _repo_specs:\n"
        "        return _repo_specs[None]\n"
        "    if 'default' in _repo_specs:\n"
        "        return _repo_specs['default']\n"
        "    _fb = MAP_REPO_VERSION_TO_SPECS.get('default', {})\n"
        "    return _fb[None] if _fb and None in _fb else {}\n"
    )

    if "_safe_specs" not in init_text:
        init_text += helper
        init_path.write_text(init_text)
        print(f"  [ok] constants/__init__.py: added _safe_specs helper")
    else:
        print(f"  [skip] constants/__init__.py: _safe_specs already present")

    # 2. Replace direct lookups across all .py files
    # Pattern variants found in swebench:
    #   MAP_REPO_VERSION_TO_SPECS[instance["repo"]][instance["version"]]["key"]
    #   MAP_REPO_VERSION_TO_SPECS[repo][version]["key"]
    import re
    pattern_instance = re.compile(
        r'MAP_REPO_VERSION_TO_SPECS\[instance\["repo"\]\]\[instance\["version"\]\]'
    )
    replacement_instance = '_safe_specs(instance["repo"], instance["version"])'

    pattern_vars = re.compile(
        r'MAP_REPO_VERSION_TO_SPECS\[repo\]\[version\]'
    )
    replacement_vars = '_safe_specs(repo, version)'

    safe_import = "from swebench.harness.constants import _safe_specs\n"

    patched = []
    for py_file in swebench_dir.rglob("*.py"):
        if "__pycache__" in str(py_file):
            continue
        text = py_file.read_text()
        original = text

        needs_import = False
        if pattern_instance.search(text):
            text = pattern_instance.sub(replacement_instance, text)
            needs_import = True

        if pattern_vars.search(text):
            text = pattern_vars.sub(replacement_vars, text)
            needs_import = True

        if needs_import and "_safe_specs" not in original:
            text = safe_import + text

        if text != original:
            py_file.write_text(text)
            patched.append(py_file.relative_to(swebench_dir))

    if patched:
        print(f"  [ok] version lookups: {', '.join(str(p) for p in patched)}")
    else:
        print(f"  [skip] version lookups already patched")


def _patch_test_patch_refs(swebench_dir: Path) -> None:
    """Replace all instance["test_patch"] with instance.get("test_patch", "")."""
    old = 'instance["test_patch"]'
    new = 'instance.get("test_patch", "")'
    patched = []
    for py_file in swebench_dir.rglob("*.py"):
        text = py_file.read_text()
        if old in text:
            text = text.replace(old, new)
            py_file.write_text(text)
            patched.append(py_file.relative_to(swebench_dir))
    if patched:
        print(f"  [ok] test_patch refs: {', '.join(str(p) for p in patched)}")
    else:
        print(f"  [skip] test_patch refs already patched")


def patch() -> None:
    """Apply all FEA-bench patches to the installed swebench package."""
    swebench_dir = _find_swebench_dir()
    print(f"Patching swebench at {swebench_dir}")

    _patch_constants_python(swebench_dir)
    _patch_constants_init(swebench_dir)
    _patch_log_parsers(swebench_dir)
    _patch_version_lookup(swebench_dir)
    _patch_version_lookups_globally(swebench_dir)
    _patch_test_patch_refs(swebench_dir)

    print("Done. swebench is now FEA-bench compatible.")


if __name__ == "__main__":
    patch()
