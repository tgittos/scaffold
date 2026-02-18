"""Install pure-Python packages from PyPI.

Gate: network
Match: package
"""

import os
import re
import tempfile
import zipfile


_MAX_DEPTH = 10


def pip_install(package: str, version: str = None, force: bool = False) -> dict:
    """Install a pure-Python package from PyPI.

    Downloads and installs packages that have py3-none-any wheels.
    C extension packages are rejected with a clear error message.
    Dependencies listed in METADATA Requires-Dist are installed recursively.

    Args:
        package: Package name to install (e.g. "six", "requests")
        version: Specific version to install (default: latest)
        force: Force reinstall even if already installed

    Returns:
        Dictionary with success status, installed packages, and any errors
    """
    import _ralph_http
    import _ralph_sys

    app_home = _ralph_sys.get_app_home()
    if not app_home:
        return {"success": False, "installed": [], "errors": ["App home not initialized"]}

    site_packages = os.path.join(app_home, "site-packages")
    os.makedirs(site_packages, exist_ok=True)

    installed = []
    errors = []
    seen = set()

    def _normalize(name):
        return re.sub(r'[-_.]+', '-', name).lower()

    def _is_installed(name):
        norm = _normalize(name)
        if not os.path.isdir(site_packages):
            return False
        for entry in os.listdir(site_packages):
            if entry.endswith(".dist-info"):
                dist_name = _name_from_dist_info(
                    os.path.join(site_packages, entry))
                if dist_name and _normalize(dist_name) == norm:
                    return True
        return False

    def _install_one(name, ver=None, depth=0):
        if depth > _MAX_DEPTH:
            errors.append(f"Dependency depth limit exceeded for '{name}'")
            return

        norm = _normalize(name)
        if norm in seen:
            return
        seen.add(norm)

        if not force and _is_installed(name):
            return

        simple_url = f"https://pypi.org/simple/{name}/"
        resp = _ralph_http.get(simple_url, headers=["Accept: text/html"], timeout=30)
        if not resp.get("ok"):
            errors.append(f"Failed to fetch package index for '{name}': HTTP {resp.get('status', 0)}")
            return

        html = resp.get("data", "")
        wheel_url, wheel_filename = _find_best_wheel(html, name, ver)
        if wheel_url is None:
            errors.append(f"No compatible wheel found for '{name}'" +
                         (f" version {ver}" if ver else "") +
                         ". Only pure-Python (py3-none-any) wheels are supported.")
            return

        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = os.path.join(tmpdir, wheel_filename)
            dl = _ralph_http.download(wheel_url, tmp_path, timeout=120)
            if not dl.get("ok"):
                errors.append(f"Failed to download {wheel_filename}")
                return

            try:
                with zipfile.ZipFile(tmp_path, 'r') as zf:
                    if not _safe_extractall(zf, site_packages, errors):
                        return
            except zipfile.BadZipFile:
                errors.append(f"Corrupted wheel file: {wheel_filename}")
                return

        installed_version = _get_installed_version(name, site_packages)
        installed.append({"name": name, "version": installed_version or "unknown"})

        deps = _get_dependencies(name, site_packages)
        for dep_name, dep_ver in deps:
            _install_one(dep_name, dep_ver, depth + 1)

    _install_one(package, version)

    return {
        "success": len(errors) == 0,
        "installed": installed,
        "errors": errors
    }


def _safe_extractall(zf, dest, errors):
    """Extract zip contents after validating all paths stay within dest."""
    real_dest = os.path.realpath(dest)
    for info in zf.infolist():
        target = os.path.realpath(os.path.join(dest, info.filename))
        if not target.startswith(real_dest + os.sep) and target != real_dest:
            errors.append(f"Unsafe path in wheel: {info.filename}")
            return False
    zf.extractall(dest)
    return True


def _find_best_wheel(html, package_name, target_version):
    """Parse PyPI simple HTML to find the best py3-none-any wheel."""
    href_pattern = re.compile(r'<a\s+href="([^"]+)"[^>]*>([^<]+)</a>', re.IGNORECASE)
    matches = href_pattern.findall(html)

    candidates = []
    for url, filename in matches:
        if not filename.endswith(".whl"):
            continue

        # Wheel format: {name}-{version}-{python}-{abi}-{platform}.whl
        # Split from right since name/version can contain hyphens
        parts = filename[:-4].rsplit("-", 3)
        if len(parts) != 4:
            continue

        name_version = parts[0]
        py_tag = parts[1]
        abi_tag = parts[2]
        plat_tag = parts[3]

        if abi_tag != "none" or plat_tag != "any":
            continue
        if not (py_tag.startswith("py3") or py_tag.startswith("py2.py3")):
            continue

        # name_version is "{name}-{version}" — split at last hyphen for version
        nv_parts = name_version.rsplit("-", 1)
        if len(nv_parts) != 2:
            continue
        whl_version = nv_parts[1]

        if target_version and whl_version != target_version:
            continue

        # Strip fragment (hash) from URL
        clean_url = url.split("#")[0]

        candidates.append((whl_version, clean_url, filename))

    if not candidates:
        return None, None

    candidates.sort(key=lambda c: _version_key(c[0]), reverse=True)
    best = candidates[0]
    return best[1], best[2]


def _version_key(version_str):
    """Convert version string to a sortable tuple.

    Pre-release suffixes (a, b, rc, dev) sort before the corresponding
    release so that stable versions are preferred.
    """
    _PRE_RELEASE = {"dev": -4, "a": -3, "alpha": -3, "b": -2, "beta": -2, "rc": -1}

    parts = []
    for segment in version_str.split("."):
        # Split numeric prefix from alpha suffix: "0rc1" -> "0", "rc1"
        m = re.match(r'^(\d+)(.*)', segment)
        if m:
            parts.append((0, int(m.group(1))))
            rest = m.group(2)
            if rest:
                # Extract pre-release tag: "rc1" -> "rc", "1"
                tag_match = re.match(r'^(dev|alpha|beta|a|b|rc)(\d*)', rest, re.I)
                if tag_match:
                    tag = tag_match.group(1).lower()
                    tag_num = int(tag_match.group(2)) if tag_match.group(2) else 0
                    parts.append((_PRE_RELEASE.get(tag, -1), tag_num))
                else:
                    parts.append((-5, rest))
        else:
            parts.append((-5, segment))
    return tuple(parts)


def _name_from_dist_info(dist_info_dir):
    """Read the package name from a dist-info METADATA file."""
    meta_path = os.path.join(dist_info_dir, "METADATA")
    if not os.path.exists(meta_path):
        return None
    with open(meta_path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            if line.startswith("Name:"):
                return line.split(":", 1)[1].strip()
            if line.strip() == '':
                break
    return None


def _get_installed_version(name, site_packages):
    """Get the installed version from dist-info METADATA."""
    norm = re.sub(r'[-_.]+', '-', name).lower()
    for entry in os.listdir(site_packages):
        if entry.endswith(".dist-info"):
            dist_name = _name_from_dist_info(
                os.path.join(site_packages, entry))
            if dist_name and re.sub(r'[-_.]+', '-', dist_name).lower() == norm:
                meta_path = os.path.join(site_packages, entry, "METADATA")
                with open(meta_path, 'r', encoding='utf-8', errors='replace') as f:
                    for line in f:
                        if line.startswith("Version:"):
                            return line.split(":", 1)[1].strip()
    return None


def _get_dependencies(name, site_packages):
    """Parse unconditional Requires-Dist from installed package METADATA."""
    norm = re.sub(r'[-_.]+', '-', name).lower()
    deps = []
    for entry in os.listdir(site_packages):
        if entry.endswith(".dist-info"):
            dist_name = _name_from_dist_info(
                os.path.join(site_packages, entry))
            if dist_name and re.sub(r'[-_.]+', '-', dist_name).lower() == norm:
                meta_path = os.path.join(site_packages, entry, "METADATA")
                with open(meta_path, 'r', encoding='utf-8', errors='replace') as f:
                    for line in f:
                        if line.startswith("Requires-Dist:"):
                            dep = line.split(":", 1)[1].strip()
                            # Skip all conditional dependencies
                            if ";" in dep:
                                continue
                            dep_spec = dep.strip()
                            m = re.match(r'^([A-Za-z0-9][-A-Za-z0-9_.]*)', dep_spec)
                            if m:
                                dep_name = m.group(1)
                                dep_ver = None
                                ver_match = re.search(r'==\s*([^\s,;\]]+)', dep_spec)
                                if ver_match:
                                    dep_ver = ver_match.group(1)
                                deps.append((dep_name, dep_ver))
    return deps
