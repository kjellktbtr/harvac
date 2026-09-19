#!/usr/bin/env python3
"""build_dos_ncd.py -- Build NCD.COM for DOS (standalone)"""

import subprocess
import sys
from pathlib import Path

# Paths
DOS_NCD_DIR = Path(__file__).resolve().parent
SRC_DIR = DOS_NCD_DIR / "src"
INCLUDE_DIR = DOS_NCD_DIR / "include"
BUILD_DIR = DOS_NCD_DIR / "build"
OUT_DIR = DOS_NCD_DIR

WATCOM = Path("/opt/watcom")
WCC = WATCOM / "binl" / "wcc"
WLINK = WATCOM / "binl" / "wlink"

# Source files
SOURCES = [
    "main.c", "video.c", "keys.c", "mem.c", "fs.c",
    "dialog.c", "panel.c", "viewer.c", "shell.c"
]

def log(msg, level="INFO"):
    prefix = f"[{level}]"
    print(f"{prefix} {msg}")

def run(cmd, cwd=None):
    log(f"Running: {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        log(f"Command failed: {result.stderr.strip()}", "ERROR")
        return False
    if result.stdout:
        log(result.stdout.strip())
    return True

def compile_source(src: Path, obj_dir: Path) -> bool:
    """Compile a C source file with OpenWatcom for DOS COM."""
    obj = obj_dir / (src.stem + ".obj")
    try:
        cmd = [
            str(WCC), "-0", "-ms", "-os", "-s", "-zl", "-d0", "-wx", "-zm",
            "-bt=com",
            f"-i={INCLUDE_DIR}",
            f"-i={WATCOM / 'h'}",
            "-fo=" + str(obj),
            str(src)
        ]
        subprocess.run(cmd, check=True, capture_output=True, text=True)
        log(f"Compiled {src.name} -> {obj.name}")
        return True
    except subprocess.CalledProcessError as e:
        log(f"Compilation failed for {src.name}: {e.stderr.strip()}", "ERROR")
        return False

def link_com(obj_dir: Path, out_dir: Path) -> bool:
    """Link objects into a COM file."""
    out_file = out_dir / "NCD.COM"
    map_file = out_dir / "NCD.map"
    temp_link = obj_dir / "_ncd_dos_link.lnk"

    with open(temp_link, "w") as f:
        f.write("format raw bin\n")
        f.write("option offset=0x100\n")
        f.write("option quiet\n")
        f.write("option eliminate\n")
        f.write("option start=main_\n")
        f.write(f"name {out_file}\n")
        f.write(f"option map={map_file}\n")
        f.write(f"library {WATCOM / 'lib286' / 'dos' / 'clibs.lib'}\n")
        f.write(f"file {WATCOM / 'lib286' / 'dos' / 'cstart_t.obj'}\n")
        for src in SOURCES:
            obj = obj_dir / (Path(src).stem + ".obj")
            if obj.exists():
                f.write(f"file {obj}\n")

    try:
        subprocess.run([str(WLINK), f"@{temp_link}"], check=True, capture_output=True, text=True)
        log(f"Linked: {out_file.name} ({out_file.stat().st_size} bytes)")

        # Verify entry point
        if map_file.exists():
            with open(map_file) as mf:
                for line in mf:
                    if "_main_" in line and "000100" in line:
                        log("Entry point OK: _main at 0x100")
                        break
            map_file.unlink()

        return True
    except subprocess.CalledProcessError as e:
        log(f"Link failed: {e.stderr.strip()}", "ERROR")
        return False

def main():
    # Create directories
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    log("=== Building DOS NCD.COM ===")

    # Compile all sources
    for src_name in SOURCES:
        src = SRC_DIR / src_name
        if not src.exists():
            log(f"Source not found: {src}", "ERROR")
            return 1
        if not compile_source(src, BUILD_DIR):
            return 1

    # Link
    if not link_com(BUILD_DIR, OUT_DIR):
        return 1

    log("=== DOS NCD.COM built successfully ===")
    return 0

if __name__ == "__main__":
    sys.exit(main())