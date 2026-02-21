"""List installed Python packages.

Gate: python
"""

import os


def pip_list() -> dict:
    """List all installed pure-Python packages.

    Scans the site-packages directory for installed packages
    by reading their dist-info metadata.

    Returns:
        Dictionary with packages list and count
    """
    import _ralph_sys

    app_home = _ralph_sys.get_app_home()
    if not app_home:
        return {"packages": [], "count": 0}

    site_packages = os.path.join(app_home, "site-packages")
    if not os.path.isdir(site_packages):
        return {"packages": [], "count": 0}

    packages = []
    for entry in sorted(os.listdir(site_packages)):
        if not entry.endswith(".dist-info"):
            continue

        meta_path = os.path.join(site_packages, entry, "METADATA")
        name = None
        version = None

        if os.path.exists(meta_path):
            with open(meta_path, 'r', encoding='utf-8', errors='replace') as f:
                for line in f:
                    if line.startswith("Name:"):
                        name = line.split(":", 1)[1].strip()
                    elif line.startswith("Version:"):
                        version = line.split(":", 1)[1].strip()
                    if name and version:
                        break

        if name:
            packages.append({
                "name": name,
                "version": version or "unknown",
                "location": site_packages
            })

    return {"packages": packages, "count": len(packages)}
