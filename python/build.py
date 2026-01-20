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
        # Validate existing file based on type
        try:
            if str(dest).endswith('.tar.gz'):
                # Validate tar.gz by attempting to open it
                with tarfile.open(dest, 'r:gz') as _:
                    pass
                print_status(f"Using existing {dest}", COLOR_GREEN)
                return
            elif dest.suffix == '.zip':
                # Validate zip by checking header
                with open(dest, 'rb') as f:
                    header = f.read(4)
                    if header[:2] == b'PK':
                        print_status(f"Using existing {dest}", COLOR_GREEN)
                        return
                print_status("Corrupted zip file, re-downloading...", COLOR_YELLOW)
                os.remove(dest)
            else:
                # For other files, assume valid if exists
                print_status(f"Using existing {dest}", COLOR_GREEN)
                return
        except Exception:
            print_status("Corrupted file, re-downloading...", COLOR_YELLOW)
            os.remove(dest)
    
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
    """Copy libpython static library to main ralph build directory for linking.

    Creates a complete static library for embedding by:
    1. Copying both x86_64 and aarch64 versions of libpython
    2. Adding required module objects (HACL, expat, mpdec) that aren't in the base library
    """
    print_status("Copying static library to main build directory...", COLOR_YELLOW)

    # Ensure ralph build directory exists
    RALPH_BUILD_DIR.mkdir(parents=True, exist_ok=True)
    aarch64_dir = RALPH_BUILD_DIR / ".aarch64"
    aarch64_dir.mkdir(parents=True, exist_ok=True)

    py_version_short = PYTHON_VERSION[:4]  # e.g., "3.12"
    lib_name = f"libpython{py_version_short}.a"

    # Find source libraries in install directory
    lib_src_x86 = install_dir / "lib" / lib_name
    lib_src_aarch64 = install_dir / "lib" / ".aarch64" / lib_name

    if not lib_src_x86.exists():
        # Try build directory as fallback
        lib_src_x86 = build_dir / lib_name
        lib_src_aarch64 = build_dir / ".aarch64" / lib_name

    if not lib_src_x86.exists():
        print_status("Warning: libpython static library not found", COLOR_YELLOW)
        print_status(f"Searched: {install_dir / 'lib'}, {build_dir}", COLOR_YELLOW)
        return

    # Copy x86_64 library
    lib_dst_x86 = RALPH_BUILD_DIR / lib_name
    shutil.copy2(lib_src_x86, lib_dst_x86)
    print_status(f"Copied {lib_name} to {RALPH_BUILD_DIR}", COLOR_GREEN)

    # Copy aarch64 library
    if lib_src_aarch64.exists():
        lib_dst_aarch64 = aarch64_dir / lib_name
        shutil.copy2(lib_src_aarch64, lib_dst_aarch64)
        print_status(f"Copied {lib_name} to {aarch64_dir}", COLOR_GREEN)
    else:
        print_status("Warning: aarch64 libpython not found", COLOR_YELLOW)

    # Add required module objects that aren't included in the base library
    # These are needed for static linking/embedding
    _add_module_objects_to_library(build_dir, lib_dst_x86, lib_dst_aarch64 if lib_src_aarch64.exists() else None)

    # Copy Python headers for ralph to include
    include_src = install_dir / "include" / f"python{py_version_short}"
    include_dst = RALPH_BUILD_DIR / "python-include"
    if include_src.exists():
        if include_dst.exists():
            shutil.rmtree(include_dst)
        shutil.copytree(include_src, include_dst)
        print_status(f"Copied Python headers to {include_dst}", COLOR_GREEN)


def _add_module_objects_to_library(build_dir, lib_x86, lib_aarch64):
    """Add additional module objects to the static library for complete embedding.

    The base libpython doesn't include all objects needed for static linking.
    We need to add: HACL (hash), expat (XML), and mpdec (decimal) objects.

    Note: Some objects have conflicting names (e.g., context.o exists in both
    Python/ and _decimal/libmpdec/). We rename conflicting objects with a prefix
    before adding them to the archive.
    """
    print_status("Adding module objects for complete static linking...", COLOR_YELLOW)

    modules_dir = build_dir / "Modules"

    # Object groups to add: (subdir, files, prefix_for_renaming)
    # The prefix is used to avoid name conflicts in the archive
    object_groups = [
        ("_hacl", ["Hacl_Hash_SHA2.o", "Hacl_Hash_SHA1.o", "Hacl_Hash_MD5.o", "Hacl_Hash_SHA3.o"], None),
        ("expat", ["xmlparse.o", "xmlrole.o", "xmltok.o"], None),
        # mpdec objects need prefix because context.o conflicts with Python/context.o
        ("_decimal/libmpdec", ["basearith.o", "constants.o", "context.o", "convolute.o",
                               "crt.o", "difradix2.o", "fnt.o", "fourstep.o", "io.o",
                               "mpalloc.o", "mpdecimal.o", "numbertheory.o", "sixstep.o",
                               "transpose.o"], "mpdec_"),
    ]

    # Create temp directory for renamed objects
    import tempfile
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)

        for subdir, obj_files, prefix in object_groups:
            obj_dir = modules_dir / subdir
            if not obj_dir.exists():
                print_status(f"Warning: {obj_dir} not found, skipping", COLOR_YELLOW)
                continue

            # Process x86_64 objects
            x86_objs = []
            for f in obj_files:
                src = obj_dir / f
                if src.exists():
                    if prefix:
                        # Rename to avoid conflicts
                        dst = tmpdir / f"{prefix}{f}"
                        shutil.copy2(src, dst)
                        x86_objs.append(str(dst))
                    else:
                        x86_objs.append(str(src))

            if x86_objs:
                cmd = f"cosmoar r {lib_x86} " + " ".join(x86_objs)
                run_command(cmd)

            # Process aarch64 objects
            if lib_aarch64:
                aarch64_obj_dir = obj_dir / ".aarch64"
                if aarch64_obj_dir.exists():
                    aarch64_objs = []
                    for f in obj_files:
                        src = aarch64_obj_dir / f
                        if src.exists():
                            if prefix:
                                dst = tmpdir / f"{prefix}aarch64_{f}"
                                shutil.copy2(src, dst)
                                aarch64_objs.append(str(dst))
                            else:
                                aarch64_objs.append(str(src))

                    if aarch64_objs:
                        cmd = f"aarch64-linux-cosmo-ar r {lib_aarch64} " + " ".join(aarch64_objs)
                        run_command(cmd)

    print_status("Module objects added to static library", COLOR_GREEN)

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
    # Use lib/python3.12/ structure to match Python's expected PYTHONHOME=/zip layout
    py_version_short = PYTHON_VERSION[:4]
    lib_src = install_dir / "lib" / f"python{py_version_short}"
    lib_dst = py_tmp / "lib" / f"python{py_version_short}"

    if lib_src.exists():
        lib_dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(lib_src, lib_dst)

        # Add sitecustomize.py
        with open(lib_dst / "sitecustomize.py", "w") as f:
            f.write(create_sitecustomize())

        # Remove config directories
        for config_dir in lib_dst.glob("config-*"):
            shutil.rmtree(config_dir)

        # Compile Python modules using the fat binary
        print_status("Compiling Python modules...", COLOR_YELLOW)
        run_command(f"{fat_python_bin} -m compileall -fqb ./lib", cwd=py_tmp)

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
    # Embedded at lib/python3.12/ to match PYTHONHOME=/zip expectations
    print_status("Adding libraries to fat binary...", COLOR_YELLOW)

    if (py_tmp / "lib").exists():
        run_command(
            f"zip -qr {output_bin.absolute()} lib/",
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
