#!/usr/bin/env python3
"""
Build portable Python using Cosmopolitan Libc.
Creates a fat binary that runs on Linux, macOS, Windows, and BSDs.
Based on superconfigure's approach.

This build shares dependencies with the main ralph project to avoid
rebuilding libraries and ensure compatibility when linking.
"""

import os
import sys
import shutil
import subprocess
import tarfile
import urllib.request
from pathlib import Path

# Configuration - matching superconfigure
PYTHON_VERSION = "3.12.3"
PYTHON_URL = f"https://github.com/python/cpython/archive/refs/tags/v{PYTHON_VERSION}.tar.gz"

# Dependency versions - MUST match main Makefile
ZLIB_VERSION = "1.3.1"
READLINE_VERSION = "8.2"
NCURSES_VERSION = "6.4"

# Build directories - local to python/
LOCAL_BUILD_DIR = Path("build").absolute()
RESULTS_DIR = LOCAL_BUILD_DIR / "results"

# Share deps with main ralph project to avoid rebuilding
RALPH_ROOT = Path(__file__).parent.parent.absolute()
DEPS_DIR = RALPH_ROOT / "deps"
RALPH_BUILD_DIR = RALPH_ROOT / "build"  # Main project build dir for outputs

# Shared dependency paths - same as main Makefile
ZLIB_DIR = DEPS_DIR / f"zlib-{ZLIB_VERSION}"
ZLIB_LIB = ZLIB_DIR / "libz.a"
READLINE_DIR = DEPS_DIR / f"readline-{READLINE_VERSION}"
READLINE_LIB = READLINE_DIR / "libreadline.a"
NCURSES_DIR = DEPS_DIR / f"ncurses-{NCURSES_VERSION}"
NCURSES_LIB = NCURSES_DIR / "lib" / "libncurses.a"

# Colors for output
COLOR_RED = "\033[91m"
COLOR_GREEN = "\033[92m"
COLOR_YELLOW = "\033[93m"
COLOR_BLUE = "\033[94m"
COLOR_RESET = "\033[0m"

def print_status(message, color=COLOR_BLUE):
    """Print colored status message."""
    print(f"{color}{message}{COLOR_RESET}", flush=True)

def run_command(cmd, cwd=None, env=None, capture_output=False):
    """Run shell command with error handling."""
    if capture_output:
        result = subprocess.run(
            cmd, 
            shell=True, 
            cwd=cwd, 
            env=env, 
            capture_output=True, 
            text=True
        )
    else:
        result = subprocess.run(
            cmd, 
            shell=True, 
            cwd=cwd, 
            env=env
        )
    
    if result.returncode != 0:
        print_status(f"Error running: {cmd}", COLOR_RED)
        if capture_output and result.stderr:
            print(result.stderr)
        sys.exit(1)
    
    if capture_output:
        return result.stdout
    return None

def download_file(url, dest):
    """Download file from URL with progress."""
    if os.path.exists(dest):
        # Check if it's a valid file
        if dest.suffix == '.zip':
            try:
                with tarfile.open(dest, 'r') if dest.suffix in ['.tar', '.gz'] else open(dest, 'rb') as f:
                    if dest.suffix == '.zip':
                        # Try to read zip header
                        header = f.read(4)
                        if header[:2] != b'PK':
                            print_status(f"Corrupted zip file, re-downloading...", COLOR_YELLOW)
                            os.remove(dest)
                        else:
                            print_status(f"Using existing {dest}", COLOR_GREEN)
                            return
            except:
                print_status(f"Corrupted file, re-downloading...", COLOR_YELLOW)
                os.remove(dest)
        else:
            print_status(f"Using existing {dest}", COLOR_GREEN)
            return
    
    print_status(f"Downloading {url}...", COLOR_YELLOW)
    try:
        # Use wget with resume capability for large files
        cmd = f"wget --progress=bar:force -c -O {dest} {url}"
        run_command(cmd)
        print_status(f"Downloaded to {dest}", COLOR_GREEN)
    except Exception as e:
        print_status(f"Failed to download {url}: {e}", COLOR_RED)
        sys.exit(1)

def download_python_source():
    """Download and extract Python source."""
    LOCAL_BUILD_DIR.mkdir(exist_ok=True)

    src_dir = LOCAL_BUILD_DIR / f"cpython-{PYTHON_VERSION}"
    if src_dir.exists():
        print_status("Python source already exists, skipping download", COLOR_GREEN)
        return src_dir
    
    tar_file = LOCAL_BUILD_DIR / f"python-{PYTHON_VERSION}.tar.gz"
    download_file(PYTHON_URL, tar_file)

    print_status("Extracting Python source...", COLOR_YELLOW)
    with tarfile.open(tar_file, "r:gz") as tar:
        tar.extractall(LOCAL_BUILD_DIR)
    
    print_status("Python source ready", COLOR_GREEN)
    return src_dir

def build_python(src_dir):
    """Build Python using cosmocc fat compiler (produces both x86_64 and aarch64)."""
    print_status("\nBuilding Python with cosmocc (fat binary)...", COLOR_BLUE)

    build_dir = LOCAL_BUILD_DIR / "build"
    install_dir = LOCAL_BUILD_DIR / "install"

    # Clean and create directories
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True)
    install_dir.mkdir(parents=True, exist_ok=True)

    # Setup environment - use cosmocc fat compiler
    env = os.environ.copy()
    env.update({
        "CC": "cosmocc",
        "CXX": "cosmoc++",
        "AR": "cosmoar",
        "RANLIB": "cosmoranlib",
    })

    print_status("Configuring Python...", COLOR_YELLOW)

    # Build include and library paths from shared deps
    include_paths = [f"-I{install_dir}/include", f"-I{ZLIB_DIR}"]
    lib_paths = [f"-L{install_dir}/lib", f"-L{ZLIB_DIR}"]

    # Add readline/ncurses if available (built by main Makefile)
    if READLINE_LIB.exists():
        include_paths.append(f"-I{READLINE_DIR}")
        include_paths.append(f"-I{READLINE_DIR}/readline")
        lib_paths.append(f"-L{READLINE_DIR}")
        print_status(f"Using shared readline from {READLINE_DIR}", COLOR_GREEN)
    if NCURSES_LIB.exists():
        include_paths.append(f"-I{NCURSES_DIR}/include")
        lib_paths.append(f"-L{NCURSES_DIR}/lib")
        print_status(f"Using shared ncurses from {NCURSES_DIR}", COLOR_GREEN)

    cflags = "-O2 " + " ".join(include_paths)
    ldflags = " ".join(lib_paths)

    configure_cmd = f"""
    {src_dir}/configure \
        --disable-shared \
        --disable-loadable-sqlite-extensions \
        --with-lto=no \
        --without-system-expat \
        --with-pymalloc \
        --disable-test-modules \
        --with-tzpath=/zip/usr/share/zoneinfo \
        --sysconfdir={install_dir}/share \
        --datarootdir={install_dir}/share \
        --prefix={install_dir} \
        ac_cv_file__dev_ptmx=yes \
        ac_cv_file__dev_ptc=no \
        CFLAGS="{cflags}" \
        LDFLAGS="{ldflags}"
    """

    run_command(configure_cmd, cwd=build_dir, env=env)

    # Copy Setup.local if it exists
    modules_dir = build_dir / "Modules"
    if Path("Setup.local").exists() and modules_dir.exists():
        shutil.copy("Setup.local", modules_dir / "Setup.local")
        print_status("Applied Setup.local", COLOR_GREEN)

    # Build
    print_status("Building Python...", COLOR_YELLOW)
    run_command("make -j4", cwd=build_dir, env=env)

    # Install
    print_status("Installing Python...", COLOR_YELLOW)
    run_command("make install", cwd=build_dir, env=env)

    # Copy static library to main ralph build directory for linking
    copy_static_library(build_dir, install_dir)

    # Find the built python binary (fat APE) and its arch-specific variants
    python_bin = install_dir / "bin" / f"python{PYTHON_VERSION[:4]}"
    if not python_bin.exists():
        python_bin = install_dir / "bin" / "python3"

    if not python_bin.exists():
        print_status("Error: Python binary not found", COLOR_RED)
        sys.exit(1)

    # cosmocc also produces .com.dbg (x86_64) and .aarch64.elf alongside the fat binary
    x86_64_bin = Path(str(python_bin) + ".com.dbg")
    aarch64_bin = Path(str(python_bin) + ".aarch64.elf")

    # If the arch-specific files don't exist in install dir, check build dir
    if not x86_64_bin.exists():
        build_python_bin = build_dir / "python"
        x86_64_bin = Path(str(build_python_bin) + ".com.dbg")
        aarch64_bin = Path(str(build_python_bin) + ".aarch64.elf")

    print_status("Python built successfully", COLOR_GREEN)
    return python_bin, x86_64_bin, aarch64_bin, install_dir


def copy_static_library(build_dir, install_dir):
    """Copy libpython static library to main ralph build directory for linking."""
    print_status("Copying static library to main build directory...", COLOR_YELLOW)

    # Ensure ralph build directory exists
    RALPH_BUILD_DIR.mkdir(parents=True, exist_ok=True)

    # Find libpython*.a in the build or install directory
    # Python build creates libpython3.X.a
    py_version_short = PYTHON_VERSION[:4]  # e.g., "3.12"

    # Check build directory first (where it's built)
    libpython_src = build_dir / f"libpython{py_version_short}.a"
    if not libpython_src.exists():
        # Check install lib directory
        libpython_src = install_dir / "lib" / f"libpython{py_version_short}.a"

    if not libpython_src.exists():
        # Try without minor version
        libpython_src = build_dir / "libpython3.a"
        if not libpython_src.exists():
            libpython_src = install_dir / "lib" / "libpython3.a"

    if libpython_src.exists():
        libpython_dst = RALPH_BUILD_DIR / f"libpython{py_version_short}.a"
        shutil.copy2(libpython_src, libpython_dst)
        print_status(f"Copied {libpython_src.name} to {RALPH_BUILD_DIR}", COLOR_GREEN)

        # Also copy Python headers for ralph to include
        include_src = install_dir / "include" / f"python{py_version_short}"
        include_dst = RALPH_BUILD_DIR / "python-include"
        if include_src.exists():
            if include_dst.exists():
                shutil.rmtree(include_dst)
            shutil.copytree(include_src, include_dst)
            print_status(f"Copied Python headers to {include_dst}", COLOR_GREEN)
    else:
        print_status("Warning: libpython static library not found", COLOR_YELLOW)
        # List what's available for debugging
        print_status(f"Build dir contents: {list(build_dir.glob('*.a'))}", COLOR_YELLOW)

def create_sitecustomize():
    """Create sitecustomize.py for /zip paths."""
    content = """import sys

def _onlyzippaths():
    L = []
    known_paths = set()
    for dir0 in sys.path:
        if dir0 not in known_paths and dir0.startswith("/zip/"):
            L.append(dir0)
            known_paths.add(dir0)
    sys.path[:] = L
    return known_paths

_onlyzippaths()
"""
    return content

def create_fat_binary(fat_python_bin, x86_64_bin, aarch64_bin, install_dir):
    """Create final fat binary with embedded standard library."""
    print_status("\nCreating final fat binary with embedded stdlib...", COLOR_BLUE)

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    results_bin = RESULTS_DIR / "bin"
    results_bin.mkdir(exist_ok=True)

    # The fat_python_bin from cosmocc is already a working fat APE
    # We just need to embed the standard library into it

    # Step 1: Prepare Python library
    print_status("Preparing Python library...", COLOR_YELLOW)

    py_tmp = RESULTS_DIR / "py-tmp"
    if py_tmp.exists():
        shutil.rmtree(py_tmp)
    py_tmp.mkdir()

    # Copy Python standard library from install dir
    lib_src = install_dir / "lib" / f"python{PYTHON_VERSION[:4]}"
    lib_dst = py_tmp / "Lib"

    if lib_src.exists():
        shutil.copytree(lib_src, lib_dst)

        # Add sitecustomize.py
        with open(lib_dst / "sitecustomize.py", "w") as f:
            f.write(create_sitecustomize())

        # Remove config directories
        for config_dir in lib_dst.glob("config-*"):
            shutil.rmtree(config_dir)

        # Compile Python modules using the fat binary
        print_status("Compiling Python modules...", COLOR_YELLOW)
        run_command(f"{fat_python_bin} -m compileall -fqb ./Lib", cwd=py_tmp)

    # Step 2: Create final binary
    # If we have arch-specific binaries, use apelink to create a clean fat binary
    # Otherwise just copy the existing fat binary
    output_bin = Path("python.com")

    if x86_64_bin.exists() and aarch64_bin.exists():
        print_status("Creating fat binary with apelink...", COLOR_YELLOW)

        # Find ape-m1.c in cosmocc installation
        ape_m1_result = subprocess.run(["which", "cosmocc"], capture_output=True, text=True)
        cosmocc_path = Path(ape_m1_result.stdout.strip())
        ape_m1 = cosmocc_path.parent / "ape-m1.c"

        cmd = f"apelink -o {output_bin} -M {ape_m1} {x86_64_bin} {aarch64_bin}"
        run_command(cmd)
    else:
        # Just copy the existing fat binary
        print_status("Copying fat binary...", COLOR_YELLOW)
        shutil.copy2(fat_python_bin, output_bin)

    # Step 3: Add Python library to the binary as ZIP
    print_status("Adding libraries to fat binary...", COLOR_YELLOW)

    if (py_tmp / "Lib").exists():
        run_command(
            f"zip -qr {output_bin.absolute()} Lib/",
            cwd=py_tmp
        )

    # Make executable
    os.chmod(output_bin, 0o755)

    print_status(f"Fat binary created: {output_bin}", COLOR_GREEN)
    return output_bin

def build_zlib():
    """Build zlib using cosmocc - shared with main ralph project."""
    # Check if already built by main Makefile
    if ZLIB_LIB.exists():
        print_status(f"Using existing zlib from {ZLIB_DIR}", COLOR_GREEN)
        return

    print_status("Building zlib...", COLOR_YELLOW)

    DEPS_DIR.mkdir(exist_ok=True)

    # Download zlib if needed
    zlib_tar = DEPS_DIR / f"zlib-{ZLIB_VERSION}.tar.gz"
    # Use GitHub mirror as zlib.net can be unreliable
    zlib_url = f"https://github.com/madler/zlib/releases/download/v{ZLIB_VERSION}/zlib-{ZLIB_VERSION}.tar.gz"

    if not zlib_tar.exists():
        print_status(f"Downloading zlib v{ZLIB_VERSION}...", COLOR_YELLOW)
        download_file(zlib_url, zlib_tar)

    # Extract zlib
    if not ZLIB_DIR.exists():
        print_status("Extracting zlib...", COLOR_YELLOW)
        with tarfile.open(zlib_tar, "r:gz") as tar:
            tar.extractall(DEPS_DIR)

    # Setup environment for cosmocc - same as main Makefile
    env = os.environ.copy()
    env.update({
        "CC": "cosmocc",
        "CFLAGS": "-O2",
    })

    # Configure and build zlib
    print_status("Configuring zlib...", COLOR_YELLOW)
    run_command(
        "./configure --static",
        cwd=ZLIB_DIR,
        env=env
    )

    print_status("Building zlib...", COLOR_YELLOW)
    run_command("make -j4 CC=cosmocc", cwd=ZLIB_DIR, env=env)

    if not ZLIB_LIB.exists():
        print_status("Error: zlib build failed", COLOR_RED)
        sys.exit(1)

    print_status("zlib built successfully", COLOR_GREEN)

def main():
    """Main build process."""
    print_status("=== Cosmopolitan Python Build ===\n", COLOR_BLUE)

    # Download Python source
    src_dir = download_python_source()

    # Build zlib (shared with main ralph project)
    print_status("\n=== Building Dependencies ===", COLOR_BLUE)
    build_zlib()

    # Build Python with cosmocc (produces fat binary for both architectures)
    print_status("\n=== Building Python ===", COLOR_BLUE)
    fat_python_bin, x86_64_bin, aarch64_bin, install_dir = build_python(src_dir)

    # Create final fat binary with embedded stdlib
    fat_binary = create_fat_binary(fat_python_bin, x86_64_bin, aarch64_bin, install_dir)

    print_status("\n=== Build Complete ===", COLOR_GREEN)
    print_status(f"Portable Python binary: {fat_binary}", COLOR_GREEN)
    print_status("Run with: ./python.com", COLOR_YELLOW)

if __name__ == "__main__":
    main()