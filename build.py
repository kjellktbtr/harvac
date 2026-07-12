#!/usr/bin/env python3
"""build.py -- Harvac Build Orchestrator

Compiles C files with OpenWatcom wcc, assembles stubs with wasm, links
the kernel flat binary with wlink, assembles bootloader with nasm, creates
a FAT16 disk image, and optionally launches QEMU.

Usage:
    python3 build.py              # Build disk image
    python3 build.py --run        # Build and launch QEMU
    python3 build.py --qemu-only  # Launch QEMU without rebuilding
    python3 build.py --clean      # Remove generated files
"""

import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

# --- Configuration ---

PROJECT_ROOT = Path(__file__).resolve().parent
SRC_DIR = PROJECT_ROOT / "src"
BUILD_DIR = PROJECT_ROOT / "build"
LD_DIR = PROJECT_ROOT / "ld"
APPS_DIR = PROJECT_ROOT / "apps"
DISK_IMAGE = PROJECT_ROOT / "harvac.img"
DISK_SIZE = 4 * 1024 * 1024  # 4 MB

WATCOM = Path("/opt/watcom")
WCC = WATCOM / "binl" / "wcc"
WASM = WATCOM / "binl" / "wasm"
WLINK = WATCOM / "binl" / "wlink"

NASM = shutil.which("nasm") or "nasm"
QEMU = shutil.which("qemu-system-i386") or "qemu-system-i386"

# Include paths
INCLUDE_DIR = SRC_DIR / "include"
WATCOM_INC = WATCOM / "h"

# Shared library directories (lib/)
LIB_DIR = PROJECT_ROOT / "lib"
LIB_POSIX_DIR = LIB_DIR / "posix"
LIB_HDK_DIR = LIB_DIR / "hdk"
LIB_INCLUDE_DIR = LIB_DIR / "include"

# Source manifests
C_SOURCES = [
    SRC_DIR / "kernel" / "kmain.c",
    SRC_DIR / "kernel" / "syscalls.c",
    SRC_DIR / "kernel" / "memory.c",
    SRC_DIR / "drivers" / "timer.c",
    SRC_DIR / "drivers" / "video.c",
    SRC_DIR / "drivers" / "serial.c",
    SRC_DIR / "drivers" / "keyboard.c",
    SRC_DIR / "drivers" / "disk.c",
    SRC_DIR / "drivers" / "fat16.c",
    SRC_DIR / "shell" / "shell.c",
    SRC_DIR / "shell" / "builtin.c",
    SRC_DIR / "shell" / "exec.c",
    SRC_DIR / "lib" / "strings.c",
    SRC_DIR / "fs" / "mount.c",
    SRC_DIR / "fs" / "vfs.c",
]

ASM_WASM_SOURCES = [
    SRC_DIR / "kernel" / "rt.asm",
    SRC_DIR / "kernel" / "entry.asm",
    SRC_DIR / "kernel" / "syscall.asm",
    SRC_DIR / "shell" / "exec_stub.asm",
]


ASM_NASM_SOURCES = [
    SRC_DIR / "boot" / "mbr.asm",
    SRC_DIR / "boot" / "vbr.asm",
]


def log(msg: str, level: str = "INFO") -> None:
    prefix = f"[{level}]"
    print(f"{prefix} {msg}")


def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess:
    log(f"Running: {' '.join(str(c) for c in cmd)}")
    return subprocess.run(cmd, check=check, capture_output=True, text=True)


def compile_c(src: Path, out_dir: Path) -> bool:
    """Compile a C file with OpenWatcom wcc (small model, freestanding)."""
    obj = out_dir / (src.stem + ".obj")
    try:
        run([
            str(WCC), "-0", "-ms", "-os", "-s", "-zl", "-d0", "-wx",
            "-bt=dos",
            f"-i={INCLUDE_DIR}",
            f"-i={LIB_INCLUDE_DIR}",
            f"-i={WATCOM_INC}",
            "-fo=" + str(obj),
            str(src),
        ])
        log(f"Compiled {src.name} -> {obj.name}")
        return True
    except subprocess.CalledProcessError as e:
        log(f"Compilation failed for {src.name}: {e.stderr.strip()}", "ERROR")
        return False


def assemble_wasm(src: Path, out_dir: Path) -> bool:
    """Assemble a WASM assembly file."""
    obj = out_dir / (src.stem + ".obj")
    try:
        run([
            str(WASM), "-ms",
            f"-i={INCLUDE_DIR}",
            f"-fo=" + str(obj),
            str(src),
        ])
        log(f"Assembled {src.name} -> {obj.name}")
        return True
    except subprocess.CalledProcessError as e:
        log(f"Assembly failed for {src.name}: {e.stderr.strip()}", "ERROR")
        return False


def assemble_nasm(src: Path, out_dir: Path) -> bool:
    """Assemble a NASM flat binary file."""
    out = out_dir / (src.stem + ".bin")
    try:
        run([NASM, "-f", "bin", "-o", str(out), str(src)])
        log(f"Assembled {src.name} -> {out.name} ({out.stat().st_size} bytes)")
        return True
    except subprocess.CalledProcessError as e:
        log(f"Assembly failed for {src.name}: {e.stderr.strip()}", "ERROR")
        return False


def link_kernel(obj_dir: Path, out_dir: Path) -> bool:
    """Link kernel with wlink using kernel.lbc linker script."""
    kernel_out = out_dir / "KERNEL.COM"

    # Collect .obj files in link order (entry must be first)
    obj_files = [
        obj_dir / "entry.obj",
        obj_dir / "rt.obj",
        obj_dir / "kmain.obj",
        obj_dir / "syscalls.obj",
        obj_dir / "memory.obj",
        obj_dir / "timer.obj",
        obj_dir / "video.obj",
        obj_dir / "serial.obj",
        obj_dir / "keyboard.obj",
        obj_dir / "disk.obj",
        obj_dir / "fat16.obj",
        obj_dir / "shell.obj",
        obj_dir / "builtin.obj",
        obj_dir / "exec.obj",
        obj_dir / "strings.obj",
        obj_dir / "mount.obj",
        obj_dir / "vfs.obj",
        obj_dir / "syscall.obj",
        obj_dir / "exec_stub.obj",
    ]

    # Verify linker script exists
    ld_script = LD_DIR / "kernel.lbc"
    if not ld_script.exists():
        log("kernel.lbc not found!", "ERROR")
        return False

    # Verify all .obj files exist
    for obj in obj_files:
        if not obj.exists():
            log(f"Missing object file: {obj}", "ERROR")
            return False

    # Build the full linker command file
    # The ld/kernel.lbc already has the FILE directives, but we override
    # by creating a temp file that includes all needed objects
    temp_ld = out_dir / "_kernel_link.lnk"
    with open(temp_ld, "w") as f:
        f.write("format raw bin\n")
        f.write("option offset=0\n")
        f.write("option quiet\n")
        f.write(f"name {kernel_out}\n")
        f.write("\n")
        for obj in obj_files:
            if obj.exists():
                rel = obj.relative_to(PROJECT_ROOT)
                f.write(f"file {rel}\n")

    try:
        run([str(WLINK), f"@{temp_ld}"])
        log(f"Kernel linked: {kernel_out.stat().st_size} bytes")
        return True
    except subprocess.CalledProcessError as e:
        log(f"Link failed: {e.stderr.strip()}", "ERROR")
        return False


def link_app(obj_dir: Path, out_dir: Path, app_name: str,
             lib_objs: "list[Path] | None" = None) -> bool:
    """Compile and link a .COM application (C source or WASM assembly).
    Tries .c first, falls back to .asm.
    lib_objs: optional list of pre-compiled library .obj files appended AFTER
    the app's own object (entry-point ordering rule: app's _main must be first)."""
    c_src = APPS_DIR / f"{app_name}.c"
    asm_src = APPS_DIR / f"{app_name}.asm"

    # Compile C source if it exists
    if c_src.exists():
        if not compile_c(c_src, obj_dir):
            return False
    elif asm_src.exists():
        if not assemble_wasm(asm_src, obj_dir):
            return False
    else:
        log(f"App source not found for '{app_name}'", "ERROR")
        return False

    # Link as flat binary (.COM format)
    app_out = out_dir / f"{app_name.upper()}.COM"
    obj = obj_dir / f"{app_name}.obj"
    temp_ld = out_dir / f"_{app_name}_link.lnk"
    with open(temp_ld, "w") as f:
        f.write("format raw bin\n")
        if c_src.exists():
            # C apps: ORG 0x100 so data references match load address
            f.write("option offset=0x100\n")
        else:
            # WASM apps: relative-to-zero, asm code adds 0x100 manually
            f.write("option offset=0\n")
        f.write("option quiet\n")
        f.write(f"name {app_out}\n")
        f.write(f"file {obj}\n")
        # lib objects AFTER app object (entry-point ordering)
        for lobj in (lib_objs or []):
            if lobj.exists():
                f.write(f"file {lobj}\n")

    try:
        run([str(WLINK), f"@{temp_ld}"])
        log(f"App linked: {app_out.name} ({app_out.stat().st_size} bytes)")
        return True
    except subprocess.CalledProcessError as e:
        log(f"App link failed for {app_name}: {e.stderr.strip()}", "ERROR")
        return False


# .COM apps placed in BIN/ on the disk image
BIN_APPS = ("HELLO", "CAT", "LS", "UNAME", "EDIT", "NCD", "XFER")

MEDIT_DIR = APPS_DIR / "medit"

MEDIT_SOURCES = [
    "main", "gap", "clip", "doc", "dlg", "edit", "menu", "search",
]


def build_medit(obj_dir: Path, out_dir: Path,
                lib_objs: "list[Path] | None" = None) -> bool:
    """Compile and link MEDIT text editor as EDIT.COM.
    lib_objs appended after app objects (entry-point ordering)."""
    objs: list[Path] = []
    for name in MEDIT_SOURCES:
        src = MEDIT_DIR / f"{name}.c"
        obj = obj_dir / f"medit_{name}.obj"
        try:
            run([
                str(WCC), "-0", "-ms", "-os", "-s", "-zl", "-d0", "-wx",
                "-bt=dos",
                f"-i={INCLUDE_DIR}",
                f"-i={LIB_INCLUDE_DIR}",
                f"-i={MEDIT_DIR}",
                "-fo=" + str(obj),
                str(src),
            ])
            log(f"Compiled {src.name} -> {obj.name}")
        except subprocess.CalledProcessError as e:
            log(f"MEDIT compile failed for {src.name}: {e.stderr.strip()}", "ERROR")
            return False
        objs.append(obj)

    edit_out = out_dir / "EDIT.COM"
    temp_ld = out_dir / "_medit_link.lnk"
    with open(temp_ld, "w") as f:
        f.write("format raw bin\n")
        f.write("option offset=0x100\n")
        f.write("option quiet\n")
        f.write(f"name {edit_out}\n")
        for obj in objs:
            f.write(f"file {obj}\n")
        for lobj in (lib_objs or []):
            if lobj.exists():
                f.write(f"file {lobj}\n")

    try:
        run([str(WLINK), f"@{temp_ld}"])
        log(f"EDIT.COM linked: {edit_out.stat().st_size} bytes")
        return True
    except subprocess.CalledProcessError as e:
        log(f"MEDIT link failed: {e.stderr.strip()}", "ERROR")
        return False


NCD_DIR = APPS_DIR / "ncd"

NCD_SOURCES = [
    "main", "fs", "panel", "viewer",
]


def build_ncd(obj_dir: Path, out_dir: Path,
              lib_objs: "list[Path] | None" = None) -> bool:
    """Compile and link NCD dual-pane file manager as NCD.COM.
    lib_objs appended after app objects (entry-point ordering)."""
    objs: list[Path] = []
    for name in NCD_SOURCES:
        src = NCD_DIR / f"{name}.c"
        obj = obj_dir / f"ncd_{name}.obj"
        try:
            run([
                str(WCC), "-0", "-ms", "-os", "-s", "-zl", "-d0", "-wx",
                "-bt=dos",
                f"-i={INCLUDE_DIR}",
                f"-i={LIB_INCLUDE_DIR}",
                f"-i={NCD_DIR}",
                "-fo=" + str(obj),
                str(src),
            ])
            log(f"Compiled {src.name} -> {obj.name}")
        except subprocess.CalledProcessError as e:
            log(f"NCD compile failed for {src.name}: {e.stderr.strip()}", "ERROR")
            return False
        objs.append(obj)

    ncd_out = out_dir / "NCD.COM"
    temp_ld = out_dir / "_ncd_link.lnk"
    with open(temp_ld, "w") as f:
        f.write("format raw bin\n")
        f.write("option offset=0x100\n")
        f.write("option quiet\n")
        f.write(f"name {ncd_out}\n")
        for obj in objs:
            f.write(f"file {obj}\n")
        for lobj in (lib_objs or []):
            if lobj.exists():
                f.write(f"file {lobj}\n")

    try:
        run([str(WLINK), f"@{temp_ld}"])
        log(f"NCD.COM linked: {ncd_out.stat().st_size} bytes")
        return True
    except subprocess.CalledProcessError as e:
        log(f"NCD link failed: {e.stderr.strip()}", "ERROR")
        return False


def build_libs(obj_dir: Path) -> list:
    """Compile all lib/posix and lib/hdk C sources into obj_dir.
    Returns list of Path objects for the resulting .obj files."""
    objs: list[Path] = []
    lib_sources = (
        sorted(LIB_POSIX_DIR.glob("*.c")) +
        sorted(LIB_HDK_DIR.glob("*.c"))
    )
    if not lib_sources:
        return objs
    for src in lib_sources:
        obj = obj_dir / f"lib_{src.stem}.obj"
        try:
            run([
                str(WCC), "-0", "-ms", "-os", "-s", "-zl", "-d0", "-wx",
                "-bt=dos",
                f"-i={INCLUDE_DIR}",
                f"-i={LIB_INCLUDE_DIR}",
                f"-i={WATCOM_INC}",
                "-fo=" + str(obj),
                str(src),
            ])
            log(f"Compiled lib {src.name} -> {obj.name}")
        except subprocess.CalledProcessError as e:
            log(f"Lib compile failed for {src.name}: {e.stderr.strip()}", "ERROR")
            return []
        objs.append(obj)
    return objs


def build_xfer(out_dir: Path) -> bool:
    """Assemble XFER.COM from apps/xfer.asm (NASM flat binary)."""
    src = APPS_DIR / "xfer.asm"
    if not src.exists():
        log("apps/xfer.asm not found", "ERROR")
        return False
    out = out_dir / "XFER.COM"
    try:
        run([NASM, "-f", "bin", "-o", str(out), str(src)])
        log(f"XFER.COM assembled: {out.stat().st_size} bytes")
        return True
    except subprocess.CalledProcessError as e:
        log(f"XFER assembly failed: {e.stderr.strip()}", "ERROR")
        return False


def build_all() -> bool:
    """Build all components. Returns True on success."""
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    OBJ_DIR = BUILD_DIR / "obj"
    OBJ_DIR.mkdir(parents=True, exist_ok=True)

    # 1. Compile C files
    for src in C_SOURCES:
        if not compile_c(src, OBJ_DIR):
            return False

    # 2. Assemble WASM stubs
    for src in ASM_WASM_SOURCES:
        if not assemble_wasm(src, OBJ_DIR):
            return False

    # 3. Link kernel
    if not link_kernel(OBJ_DIR, BUILD_DIR):
        return False

    # 3b. Compile shared libraries (lib/posix, lib/hdk)
    lib_objs = build_libs(OBJ_DIR)

    # 3c. Build apps (.COM executables) — lib objects linked after app object
    for app in ("hello", "cat", "ls", "uname", "shell"):
        if not link_app(OBJ_DIR, BUILD_DIR, app, lib_objs):
            log(f"App build ({app}) skipped", "WARNING")

    # 3d. Build MEDIT (multi-file editor)
    if not build_medit(OBJ_DIR, BUILD_DIR, lib_objs):
        log("MEDIT build skipped", "WARNING")

    # 3e. Build NCD (dual-pane file manager)
    if not build_ncd(OBJ_DIR, BUILD_DIR, lib_objs):
        log("NCD build skipped", "WARNING")

    # 3f. Assemble XFER serial file-transfer agent
    if not build_xfer(BUILD_DIR):
        log("XFER build skipped", "WARNING")

    # 4. Assemble boot files (NASM)
    for src in ASM_NASM_SOURCES:
        if not assemble_nasm(src, BUILD_DIR):
            return False

    return True


def create_disk_image() -> Path:
    """Create a raw disk image filled with zeros."""
    log(f"Creating {DISK_SIZE / (1024 * 1024):.0f}MB disk image...")
    with open(DISK_IMAGE, "wb") as f:
        f.write(b"\x00" * DISK_SIZE)
    return DISK_IMAGE


def write_mbr(image: Path, mbr_bin: Path) -> None:
    """Write MBR to sector 0 of disk image."""
    data = mbr_bin.read_bytes()
    if len(data) < 512:
        data = data + b"\x00" * (512 - len(data))
    with open(image, "r+b") as f:
        f.seek(0)
        f.write(data)
    log("MBR written to sector 0")


def create_fat16_partition(image: Path, vbr_bin: Path) -> int:
    """Create a FAT16 partition starting at LBA 63."""
    PARTITION_START = 63  # LBA
    SECTOR_SIZE = 512

    # FAT16 geometry
    RESERVED = 1        # Reserved sectors (the VBR itself)
    FAT_COUNT = 2
    # Max FAT16 entries per 512-byte sector = 256 (2 bytes each)
    # We use 32 sectors per FAT => 32 * 256 = 8192 entries, enough for the
    # ~8032 data clusters of a 4MB image (FAT16 needs >= 4085 clusters)
    FAT_SECTORS = 32
    ROOT_ENTRIES = 512  # Max root directory entries
    ROOT_SECTORS = (ROOT_ENTRIES * 32 + SECTOR_SIZE - 1) // SECTOR_SIZE  # 32
    TOTAL_SECTORS = 8192  # 4MB / 512

    # Calculate derived values
    FIRST_DATA_SECTOR = (PARTITION_START + RESERVED +
                         FAT_COUNT * FAT_SECTORS + ROOT_SECTORS)
    TOTAL_CLUSTERS = ((TOTAL_SECTORS - (FIRST_DATA_SECTOR - PARTITION_START))
                      // 1)  # sectors per cluster = 1

    log(f"FAT16: {FAT_SECTORS} FAT sectors, {ROOT_ENTRIES} root entries, "
        f"{TOTAL_CLUSTERS} clusters")

    # Read BPB template from VBR binary
    vbr_data = vbr_bin.read_bytes()
    if len(vbr_data) < 512:
        vbr_data = vbr_data + b"\x00" * (512 - len(vbr_data))

    # Build BPB
    oem_name = b"HARVAC   "  # 8 bytes
    bytes_per_sector = SECTOR_SIZE.to_bytes(2, 'little')
    sectors_per_cluster = (1).to_bytes(1, 'little')
    reserved_sectors = RESERVED.to_bytes(2, 'little')
    fat_count = FAT_COUNT.to_bytes(1, 'little')
    root_entries = ROOT_ENTRIES.to_bytes(2, 'little')
    # < 65536 sectors: use the 16-bit field, zero the 32-bit one (FAT convention)
    total_sectors_small = TOTAL_SECTORS.to_bytes(2, 'little')
    media_descriptor = (0xF8).to_bytes(1, 'little')  # Fixed disk
    fat_size_sectors = FAT_SECTORS.to_bytes(2, 'little')
    sectors_per_track = (63).to_bytes(2, 'little')
    head_count = (255).to_bytes(2, 'little')
    hidden_sectors = PARTITION_START.to_bytes(4, 'little')
    total_sectors_large = (0).to_bytes(4, 'little')  # unused: 16-bit field is set

    # Extended BPB (FAT16)
    drive_number = (0x80).to_bytes(1, 'little')  # Hard drive
    reserved_byte = (0).to_bytes(1, 'little')
    boot_signature = (0x29).to_bytes(1, 'little')
    volume_serial = 0x20250705.to_bytes(4, 'little')
    volume_label = b"HARVAC     "  # 11 bytes
    fs_type = b"FAT16   "  # 8 bytes

    bpb = (
        b"\xEB\x3C\x90"  # Short jump past BPB to VBR code at 0x3B
        + oem_name
        + bytes_per_sector
        + sectors_per_cluster
        + reserved_sectors
        + fat_count
        + root_entries
        + total_sectors_small
        + media_descriptor
        + fat_size_sectors
        + sectors_per_track
        + head_count
        + hidden_sectors
        + total_sectors_large
        + drive_number
        + reserved_byte
        + boot_signature
        + volume_serial
        + volume_label
        + fs_type
    )

    # Build VBR sector: BPB (0-59) + boot code at 0x3B
    # The jump at offset 0 jumps to 0x3B, skipping the BPB
    vbr_sector = bytearray(512)
    # Write BPB bytes at offsets 0-59 (size varies, but we overwrite what's needed)
    vbr_sector[0:3] = bpb[0:3]    # Jump instruction
    vbr_sector[3:11] = bpb[3:11]  # OEM name
    # Write the rest of BPB at standard offsets
    vbr_sector[11:13] = bytes_per_sector
    vbr_sector[13] = sectors_per_cluster[0]
    vbr_sector[14:16] = reserved_sectors
    vbr_sector[16] = fat_count[0]
    vbr_sector[17:19] = root_entries
    vbr_sector[19:21] = total_sectors_small
    vbr_sector[21] = media_descriptor[0]
    vbr_sector[22:24] = fat_size_sectors
    vbr_sector[24:26] = sectors_per_track
    vbr_sector[26:28] = head_count
    vbr_sector[28:32] = hidden_sectors
    vbr_sector[32:36] = total_sectors_large
    vbr_sector[36] = drive_number[0]
    vbr_sector[37] = reserved_byte[0]
    vbr_sector[38] = boot_signature[0]
    vbr_sector[39:43] = volume_serial
    vbr_sector[43:54] = volume_label
    vbr_sector[54:62] = fs_type

    # Write VBR boot code at offset 0x3B (59 decimal)
    # vbr_data starts at org 0x3B, so the code itself starts at vbr_data[0]
    boot_code_len = min(len(vbr_data), 512 - 0x3B - 2)  # Leave room for sig
    vbr_sector[0x3B:0x3B + boot_code_len] = vbr_data[:boot_code_len]

    # Boot signature at end of sector
    vbr_sector[510] = 0x55
    vbr_sector[511] = 0xAA

    # Write VBR to partition start (LBA 63)
    lba_offset = PARTITION_START * SECTOR_SIZE
    with open(image, "r+b") as f:
        f.seek(lba_offset)
        f.write(vbr_sector)
    log(f"VBR written at LBA {PARTITION_START}")

    # Create empty FATs (mark cluster 0 and 1 as reserved)
    fat_entry_media = media_descriptor[0] | 0xFF00  # 0xFFF8
    fat_entry_eoc = 0xFFFF

    fat_data = bytearray(FAT_SECTORS * SECTOR_SIZE)
    struct.pack_into('<H', fat_data, 0, fat_entry_media)  # Cluster 0: media descriptor
    struct.pack_into('<H', fat_data, 2, fat_entry_eoc)    # Cluster 1: EOC

    # Write FAT copies
    for i in range(FAT_COUNT):
        fat_lba = PARTITION_START + RESERVED + i * FAT_SECTORS
        with open(image, "r+b") as f:
            f.seek(fat_lba * SECTOR_SIZE)
            f.write(fat_data)
        log(f"FAT {i} written at LBA {fat_lba}")

    # Create empty root directory
    root_lba = PARTITION_START + RESERVED + FAT_COUNT * FAT_SECTORS
    root_data = b"\x00" * (ROOT_SECTORS * SECTOR_SIZE)
    with open(image, "r+b") as f:
        f.seek(root_lba * SECTOR_SIZE)
        f.write(root_data)
    log(f"Root directory written at LBA {root_lba}")

    return FIRST_DATA_SECTOR


def _fat16_datetime(mtime: float) -> tuple[int, int]:
    """Convert Unix mtime to FAT16 date/time words."""
    from datetime import datetime
    dt = datetime.fromtimestamp(mtime)
    fat_date = ((dt.year - 1980) << 9) | (dt.month << 5) | dt.day
    fat_time = (dt.hour << 11) | (dt.minute << 5) | (dt.second // 2)
    return fat_date, fat_time


def write_file_to_fat16(
    image: Path,
    data: bytes,
    filename: str,
    first_data_sector: int,
    sectors_per_cluster: int = 1,
    mtime: float | None = None,
) -> bool:
    """Write a file to the FAT16 root directory and allocate clusters."""
    SECTOR_SIZE = 512
    PARTITION_START = 63
    RESERVED = 1
    FAT_COUNT = 2
    FAT_SECTORS = 32
    ROOT_ENTRIES = 512
    ROOT_SECTORS = 32

    root_lba = PARTITION_START + RESERVED + FAT_COUNT * FAT_SECTORS
    first_data_lba = first_data_sector
    _ = sectors_per_cluster  # unused in current implementation

    # Compute FAT16 date/time from mtime, or use current time
    if mtime is not None:
        fat_date, fat_time = _fat16_datetime(mtime)
    else:
        from datetime import datetime
        now = datetime.now()
        fat_date = ((now.year - 1980) << 9) | (now.month << 5) | now.day
        fat_time = (now.hour << 11) | (now.minute << 5) | (now.second // 2)

    # Calculate number of clusters needed
    cluster_size = sectors_per_cluster * SECTOR_SIZE
    clusters_needed = (len(data) + cluster_size - 1) // cluster_size

    if clusters_needed == 0:
        return True

    # Open image and find free clusters
    with open(image, "r+b") as f:
        # Step 1: Scan FAT for free clusters
        fat_lba = PARTITION_START + RESERVED
        fat_size = FAT_SECTORS * SECTOR_SIZE

        # Read FAT
        f.seek(fat_lba * SECTOR_SIZE)
        fat = bytearray(f.read(fat_size))

        # Find free clusters
        free_clusters = []
        for cluster in range(2, FAT_SECTORS * 256):  # Max FAT16 entries
            entry_offset = cluster * 2
            if entry_offset + 2 > len(fat):
                break
            entry = struct.unpack_from('<H', fat, entry_offset)[0]
            if entry == 0x0000:  # Free
                free_clusters.append(cluster)
                if len(free_clusters) == clusters_needed:
                    break

        if len(free_clusters) < clusters_needed:
            log(f"Not enough free clusters! Need {clusters_needed}, "
                f"found {len(free_clusters)}", "ERROR")
            return False

        log(f"Allocating {clusters_needed} clusters starting at "
            f"#{free_clusters[0]}")

        # Step 2: Write FAT entries (chain)
        for i in range(clusters_needed):
            next_cluster = free_clusters[i + 1] if i + 1 < clusters_needed else 0xFFF8
            offset = free_clusters[i] * 2
            struct.pack_into('<H', fat, offset, next_cluster)

        # Step 3: Write FAT copies
        for i in range(FAT_COUNT):
            lba = PARTITION_START + RESERVED + i * FAT_SECTORS
            f.seek(lba * SECTOR_SIZE)
            f.write(fat)

        # Step 4: Write file data to clusters
        written = 0
        for i in range(clusters_needed):
            cluster = free_clusters[i]
            lba = first_data_lba + (cluster - 2) * sectors_per_cluster
            chunk = data[written:written + cluster_size]
            f.seek(lba * SECTOR_SIZE)
            f.write(chunk)
            written += len(chunk)

        log(f"Wrote {written} bytes to data area")

        # Step 5: Create directory entry in root
        base, _, f_ext = filename.upper().partition(".")
        fname = (base + "        ")[:8]  # 8 chars, space-padded
        ext = (f_ext + "   ")[:3]        # 3 chars, space-padded
        dir_name = fname.encode("ascii")
        dir_ext = ext.encode("ascii")  # 3 chars

        # Search root directory for empty entry (first byte 0x00 or 0xE5)
        dir_entry = None
        for entry_idx in range(ROOT_ENTRIES):
            offset = root_lba * SECTOR_SIZE + entry_idx * 32
            f.seek(offset)
            first_byte = f.read(1)
            if first_byte in (b"\x00", b"\xE5"):  # Free or deleted
                dir_entry = offset
                break

        if dir_entry is None:
            log("Root directory full!", "ERROR")
            return False

        # Build directory entry
        entry = bytearray(32)
        entry[0:8] = dir_name
        entry[8:11] = dir_ext
        entry[11] = 0x20  # Archive attribute
        # FAT16 date/time
        entry[22:24] = struct.pack('<H', fat_time)
        entry[24:26] = struct.pack('<H', fat_date)
        entry[26:28] = struct.pack('<H', free_clusters[0])  # First cluster
        entry[28:32] = struct.pack('<I', len(data))  # File size

        f.seek(dir_entry)
        f.write(entry)
        log(f"Directory entry for {filename} at root offset {dir_entry}")

    return True


# ─── FAT16 geometry (must match create_fat16_partition) ───
_PARTITION_START = 63
_FAT_COUNT = 2
_FAT_SECTORS = 32
_ROOT_ENTRIES = 512
_ROOT_SECTORS = 32
_SECTOR_SIZE = 512


def _read_sector(image: Path, lba: int) -> bytearray:
    """Read one 512-byte sector from the disk image."""
    with open(image, "r+b") as f:
        f.seek(lba * _SECTOR_SIZE)
        return bytearray(f.read(_SECTOR_SIZE))


def _write_sector(image: Path, lba: int, data: bytes) -> None:
    """Write one 512-byte sector to the disk image."""
    with open(image, "r+b") as f:
        f.seek(lba * _SECTOR_SIZE)
        f.write(data[:_SECTOR_SIZE])


def _read_fat(image: Path) -> bytearray:
    """Read FAT copy 0."""
    fat_lba = _PARTITION_START + 1  # Reserved (VBR) at LBA 63, FAT0 at 64
    return _read_sector(image, fat_lba)


def _write_fat(image: Path, fat: bytearray) -> None:
    """Write both FAT copies."""
    fat_lba = _PARTITION_START + 1
    _write_sector(image, fat_lba, bytes(fat))
    _write_sector(image, fat_lba + _FAT_SECTORS, bytes(fat))


def _find_free_clusters(image: Path, count: int) -> list[int]:
    """Scan FAT for free clusters."""
    fat = _read_fat(image)
    clusters = []
    for cluster in range(2, _FAT_SECTORS * 256):
        off = cluster * 2
        if off + 2 > len(fat):
            break
        entry = struct.unpack_from('<H', fat, off)[0]
        if entry == 0x0000:
            clusters.append(cluster)
            if len(clusters) == count:
                break
    return clusters


def _first_data_lba(first_data_sector: int) -> int:
    return first_data_sector


def create_directory(image: Path, name: str, first_data_sector: int) -> bool:
    """Create a directory in the root directory. Name is 8.3 uppercase (no ext)."""
    if len(name) > 8:
        name = name[:8]
    name = name.upper().ljust(8)

    clusters = _find_free_clusters(image, 1)
    if not clusters:
        log("No free clusters for directory", "ERROR")
        return False
    cluster = clusters[0]

    # Initialize the directory cluster with '.' and '..' entries
    data_lba = first_data_sector + (cluster - 2)
    sector = bytearray(512)
    # Entry 0: "." (points to self)
    sector[0] = ord('.')
    for i in range(1, 8):
        sector[i] = 0x20  # space
    for i in range(8, 11):
        sector[i] = 0x20  # space
    sector[11] = 0x10  # DIRECTORY attribute
    sector[26] = cluster & 0xFF
    sector[27] = (cluster >> 8) & 0xFF
    # Entry 1: ".." (points to root, first_cluster=0)
    sector[32] = ord('.')
    sector[33] = ord('.')
    for i in range(34, 40):
        sector[i] = 0x20
    for i in range(40, 43):
        sector[i] = 0x20
    sector[43] = 0x10  # DIRECTORY attribute
    # first_cluster=0 means root

    _write_sector(image, data_lba, bytes(sector))

    # Mark cluster as EOC in FAT
    fat = _read_fat(image)
    struct.pack_into('<H', fat, cluster * 2, 0xFFF8)
    _write_fat(image, fat)

    # Create root directory entry
    root_lba = _PARTITION_START + 1 + _FAT_COUNT * _FAT_SECTORS
    entry_data = bytearray(32)
    entry_data[0:8] = name.encode("ascii")
    entry_data[8:11] = b"   "   # space-pad extension (FAT 8.3 format)
    entry_data[11] = 0x10  # DIRECTORY attribute
    entry_data[26] = cluster & 0xFF
    entry_data[27] = (cluster >> 8) & 0xFF

    # Find free slot in root directory
    for entry_idx in range(_ROOT_ENTRIES):
        offset = root_lba * _SECTOR_SIZE + entry_idx * 32
        with open(image, "r+b") as f:
            f.seek(offset)
            first_byte = f.read(1)
            if first_byte in (b"\x00", b"\xE5"):
                f.seek(offset)
                f.write(entry_data)
                log(f"Directory entry '{name}' at root offset {offset}")
                return True

    log("Root directory full!", "ERROR")
    return False


def find_directory_cluster(image: Path, name: str,
                            first_data_sector: int) -> int | None:
    """Find the first cluster of a root directory entry."""
    _ = first_data_sector  # unused
    root_lba = _PARTITION_START + 1 + _FAT_COUNT * _FAT_SECTORS
    name_padded = name.upper().ljust(8).encode("ascii")

    with open(image, "r+b") as f:
        for entry_idx in range(_ROOT_ENTRIES):
            f.seek(root_lba * _SECTOR_SIZE + entry_idx * 32)
            entry_name = f.read(8)
            _ = f.read(3)  # skip extension (bytes 8-10)
            attrs_byte = f.read(1)  # byte 11
            if entry_name == b"\x00":
                break
            if entry_name[0:1] == b"\xE5":
                continue
            if entry_name == name_padded and (attrs_byte[0] & 0x10):
                # Read first cluster
                f.seek(root_lba * _SECTOR_SIZE + entry_idx * 32 + 26)
                cluster_bytes = f.read(2)
                return struct.unpack('<H', cluster_bytes)[0]
    return None


def write_file_to_subdir(image: Path, data: bytes, filename: str,
                          first_data_sector: int, dir_cluster: int) -> bool:
    """Write a file into a subdirectory given its first cluster number."""
    # Parse 8.3 name
    base, _, ext = filename.upper().partition(".")
    fname = (base + "        ")[:8]
    fext = (ext + "   ")[:3]

    # Allocate clusters
    clusters_needed = (len(data) + _SECTOR_SIZE - 1) // _SECTOR_SIZE
    if clusters_needed == 0:
        return True
    clusters = _find_free_clusters(image, clusters_needed)
    if len(clusters) < clusters_needed:
        log(f"Not enough clusters for {filename} in subdir", "ERROR")
        return False

    # Write FAT chain
    fat = _read_fat(image)
    for i in range(clusters_needed):
        next_c = clusters[i + 1] if i + 1 < clusters_needed else 0xFFF8
        struct.pack_into('<H', fat, clusters[i] * 2, next_c)
    _write_fat(image, fat)

    # Write data to clusters
    written = 0
    for i in range(clusters_needed):
        lba = first_data_sector + (clusters[i] - 2)
        chunk = data[written:written + _SECTOR_SIZE]
        _write_sector(image, lba, bytes(chunk) if len(chunk) == _SECTOR_SIZE
                      else chunk + b"\x00" * (_SECTOR_SIZE - len(chunk)))
        written += len(chunk)

    # Find free entry slot in the subdirectory
    cluster = dir_cluster
    while cluster >= 2 and cluster < 0xFFF8:
        sector = _read_sector(image, first_data_sector + (cluster - 2))
        for i in range(16):
            if sector[i * 32] in (0x00, 0xE5):
                # Found free slot - write entry
                entry = bytearray(32)
                entry[0:8] = fname.encode("ascii")
                entry[8:11] = fext.encode("ascii")
                entry[11] = 0x20  # Archive
                # FAT16 date/time: use current time for subdir entries
                from datetime import datetime
                _now = datetime.now()
                _fat_date = ((_now.year - 1980) << 9) | (_now.month << 5) | _now.day
                _fat_time = (_now.hour << 11) | (_now.minute << 5) | (_now.second // 2)
                entry[22:24] = struct.pack('<H', _fat_time)
                entry[24:26] = struct.pack('<H', _fat_date)
                entry[26] = clusters[0] & 0xFF
                entry[27] = (clusters[0] >> 8) & 0xFF
                entry[28:32] = struct.pack('<I', len(data))

                sector[i * 32:(i * 32) + 32] = entry
                _write_sector(image, first_data_sector + (cluster - 2),
                              bytes(sector))
                log(f"Wrote {filename} to subdir cluster {dir_cluster}")
                return True

        # If sector full and last entry != 0x00 (more entries possible),
        # follow chain to next cluster
        if sector[15 * 32] != 0x00:
            fat = _read_fat(image)
            next_c = struct.unpack('<H', fat[cluster * 2:(cluster * 2) + 2])[0]
            if next_c >= 2 and next_c < 0xFFF8:
                cluster = next_c
                continue
        break

    log(f"Subdirectory full for {filename}", "ERROR")
    return False


def clean() -> None:
    """Remove build artifacts."""
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
        log(f"Removed {BUILD_DIR}/")
    if DISK_IMAGE.exists():
        DISK_IMAGE.unlink()
        log(f"Removed {DISK_IMAGE}")


def run_tests() -> int:
    """Build disk image and run automated smoke tests in QEMU."""
    # Step 1: Build
    log("=== Test: Build ===")
    if not build_all():
        log("Build failed", "ERROR")
        return 1

    log("=== Test: Create Disk Image ===")
    image = create_disk_image()
    mbr_path = BUILD_DIR / "mbr.bin"
    vbr_path = BUILD_DIR / "vbr.bin"

    if mbr_path.exists():
        write_mbr(image, mbr_path)
    else:
        log("MBR not found", "ERROR")
        return 1

    if vbr_path.exists():
        first_data = create_fat16_partition(image, vbr_path)
    else:
        log("VBR not found", "ERROR")
        return 1

    kernel_path = BUILD_DIR / "KERNEL.COM"
    if kernel_path.exists():
        kernel_data = kernel_path.read_bytes()
        if not write_file_to_fat16(image, kernel_data, "KERNEL.COM", first_data):
            log("Failed to write KERNEL.COM", "ERROR")
            return 1
    else:
        log("KERNEL.COM not found", "ERROR")
        return 1

    for app_name in ["HELLO.COM", "CAT.COM", "LS.COM", "UNAME.COM"]:
        app_path = BUILD_DIR / app_name
        if app_path.exists():
            app_data = app_path.read_bytes()
            write_file_to_fat16(image, app_data, app_name, first_data)

    # Inject SHELL.COM (user-space shell)
    shell_path = BUILD_DIR / "SHELL.COM"
    if shell_path.exists():
        shell_data = shell_path.read_bytes()
        if write_file_to_fat16(image, shell_data, "SHELL.COM", first_data):
            log("SHELL.COM written to disk image")
        else:
            log("Failed to write SHELL.COM!", "WARNING")
    else:
        log("SHELL.COM not found!", "WARNING")

    # Inject README.TXT
    readme_path = BUILD_DIR / "README.TXT"
    if readme_path.exists():
        readme_data = readme_path.read_bytes()
        write_file_to_fat16(image, readme_data, "README.TXT", first_data)

    log(f"Disk image created: {image} ({image.stat().st_size} bytes)")

    # Step 2: Launch QEMU and verify boot output
    log("=== Test: QEMU Boot ===")
    import subprocess

    proc = subprocess.Popen(
        [QEMU, "-display", "none", "-serial", "stdio",
         "-m", "4", "-drive", f"file={DISK_IMAGE},format=raw"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )

    try:
        stdout_data, _ = proc.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout_data, _ = proc.communicate()

    tests_passed = 0
    tests_total = 0

    checks = [
        ("Harvac kernel booted", "Kernel boot message"),
        ("Harvac Shell", "Shell prompt"),
    ]

    for marker, name in checks:
        tests_total += 1
        if marker in stdout_data:
            log(f"  PASS: {name}")
            tests_passed += 1
        else:
            log(f"  FAIL: {name} (expected '{marker}')", "WARN")

    log(f"\nResults: {tests_passed}/{tests_total} tests passed")
    return 0 if tests_passed == tests_total else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Harvac Build Orchestrator")
    parser.add_argument("--run", action="store_true", help="Build and run QEMU")
    parser.add_argument("--qemu-only", action="store_true",
                        help="Run QEMU without building")
    parser.add_argument("--display", action="store_true",
                        help="Run QEMU with VGA display + PS/2 keyboard (no serial stdio)")
    parser.add_argument("--xfer-run", action="store_true",
                        help="Run QEMU with VGA display + COM1 on Unix socket for XFER")
    parser.add_argument("--clean", action="store_true", help="Clean artifacts")
    parser.add_argument("--check", action="store_true",
                        help="Compile all sources without linking (syntax check)")
    parser.add_argument("--test", action="store_true",
                        help="Build and run automated smoke tests")
    args = parser.parse_args()

    if args.clean:
        clean()
        return 0

    if args.check:
        log("=== Syntax Check ===")
        ok = True
        for src in C_SOURCES:
            if not compile_c(src, BUILD_DIR / "obj"):
                ok = False
        for src in ASM_WASM_SOURCES:
            if not assemble_wasm(src, BUILD_DIR / "obj"):
                ok = False
        for src in ASM_NASM_SOURCES:
            if not assemble_nasm(src, BUILD_DIR):
                ok = False
        if ok:
            log("Syntax check passed")
            return 0
        return 1

    if args.test:
        return run_tests()

    if args.qemu_only:
        if not DISK_IMAGE.exists():
            log("No disk image found. Build first.", "ERROR")
            return 1
        log("Launching QEMU...")
        subprocess.run([
            QEMU, "-display", "none", "-serial", "stdio",
            "-m", "4", "-drive", f"file={DISK_IMAGE},format=raw"
        ])
        return 0

    if args.display:
        if not DISK_IMAGE.exists():
            log("No disk image found. Build first.", "ERROR")
            return 1
        log("Launching QEMU with VGA display...")
        subprocess.run([
            QEMU, "-m", "4", "-drive", f"file={DISK_IMAGE},format=raw"
        ])
        return 0

    if args.xfer_run:
        if not DISK_IMAGE.exists():
            log("No disk image found. Build first.", "ERROR")
            return 1
        sock = "/tmp/harvac-xfer.sock"
        log(f"Launching QEMU with VGA display + COM1 socket at {sock}")
        log("Connect from host with: python host.py --socket " + sock)
        subprocess.run([
            QEMU, "-m", "4", "-drive", f"file={DISK_IMAGE},format=raw",
            "-serial", f"unix:{sock},server=on,wait=off",
        ])
        return 0

    # Build
    log("=== Harvac Build ===")
    if not build_all():
        log("Build failed!", "ERROR")
        return 1

    # Create disk image
    log("=== Creating Disk Image ===")
    image = create_disk_image()

    # Write MBR
    mbr_path = BUILD_DIR / "mbr.bin"
    if mbr_path.exists():
        write_mbr(image, mbr_path)
    else:
        log("MBR binary not found!", "ERROR")
        return 1

    # Create FAT16 partition and write VBR
    vbr_path = BUILD_DIR / "vbr.bin"
    if vbr_path.exists():
        first_data: int = create_fat16_partition(image, vbr_path)
    else:
        log("VBR binary not found!", "ERROR")
        return 1

    # Inject kernel
    kernel_path = BUILD_DIR / "KERNEL.COM"
    if kernel_path.exists():
        kernel_data = kernel_path.read_bytes()
        if write_file_to_fat16(image, kernel_data, "KERNEL.COM", first_data):
            log("KERNEL.COM written to disk image")
        else:
            log("Failed to write KERNEL.COM!", "ERROR")
            return 1
    else:
        log("KERNEL.COM not found!", "ERROR")
        return 1

    # Inject SHELL.COM (user-space shell)
    shell_path = BUILD_DIR / "SHELL.COM"
    if shell_path.exists():
        shell_data = shell_path.read_bytes()
        if write_file_to_fat16(image, shell_data, "SHELL.COM", first_data):
            log("SHELL.COM written to disk image")
        else:
            log("Failed to write SHELL.COM!", "WARNING")

    # Create Linux-style directory structure
    for dir_name in ("BIN", "HOME", "TMP", "DOCS"):
        if create_directory(image, dir_name, first_data):
            log(f"Directory '{dir_name}' created")
        else:
            log(f"Failed to create directory '{dir_name}'!", "WARNING")

    # Write .COM apps directly into BIN/
    bin_cluster = find_directory_cluster(image, "BIN", first_data)
    if bin_cluster:
        for app_name in BIN_APPS:
            app_path = BUILD_DIR / f"{app_name}.COM"
            if not app_path.exists():
                log(f"{app_name}.COM not found, skipping", "WARNING")
                continue
            app_data = app_path.read_bytes()
            if write_file_to_subdir(image, app_data, f"{app_name}.COM",
                                    first_data, bin_cluster):
                log(f"{app_name}.COM written to BIN/")
            else:
                log(f"Failed to write {app_name}.COM to BIN/!", "WARNING")

        # Also write NCD.COM to root for direct execution
        ncd_path = BUILD_DIR / "NCD.COM"
        if ncd_path.exists():
            ncd_data = ncd_path.read_bytes()
            if write_file_to_fat16(image, ncd_data, "NCD.COM", first_data):
                log("NCD.COM written to root")
    else:
        log("BIN directory not found!", "WARNING")

    # Copy os-docs/ documentation into DOCS/ as CRLF ASCII text
    docs_cluster = find_directory_cluster(image, "DOCS", first_data)
    if docs_cluster:
        for md_name, txt_name in (("MANUAL.md", "MANUAL.TXT"),
                                  ("EDIT.md", "EDIT.TXT"),
                                  ("NCD.md", "NCD.TXT"),
                                  ("XFER.md", "XFER.TXT")):
            md_path = PROJECT_ROOT / "os-docs" / md_name
            if not md_path.exists():
                log(f"os-docs/{md_name} not found!", "WARNING")
                continue
            text = md_path.read_text(encoding="utf-8")
            for uni, asc in (("─", "-"), ("│", "|"), ("┌", "+"), ("┐", "+"),
                             ("└", "+"), ("┘", "+"), ("├", "+"), ("┤", "+"),
                             ("↔", "<->"), ("·", "*"), ("≤", "<="),
                             ("≥", ">="), ("—", "--"), ("–", "-"),
                             ("“", '"'), ("”", '"'), ("’", "'"), ("‘", "'")):
                text = text.replace(uni, asc)
            doc_data = text.encode("ascii", "replace").replace(b"\n", b"\r\n")
            if write_file_to_subdir(image, doc_data, txt_name,
                                    first_data, docs_cluster):
                log(f"{txt_name} written to DOCS/")
            else:
                log(f"Failed to write {txt_name} to DOCS/!", "WARNING")
    else:
        log("DOCS directory not found!", "WARNING")

    # Inject sample files
    readme_data = b"Harvac OS v0.2\r\n"
    readme_data += b"OpenWatcom C kernel with FAT16 filesystem\r\n"
    readme_data += b"\r\n"
    readme_data += b"Apps in BIN/: HELLO, CAT, LS, UNAME, EDIT, NCD, XFER\r\n"
    readme_data += b"Manual: cd DOCS  then  cat MANUAL.TXT (also NCD.TXT)\r\n"
    if write_file_to_fat16(image, readme_data, "README.TXT", first_data):
        log("README.TXT written to disk image")

    demo_bat = (
        b"@echo off\r\n"
        b"echo Hello from a batch script\r\n"
        b"ls\r\n"
        b"cat readme.txt\r\n"
        b"echo Done\r\n"
    )
    if write_file_to_fat16(image, demo_bat, "DEMO.BAT", first_data):
        log("DEMO.BAT written to disk image")

    log(f"Disk image created: {image} ({image.stat().st_size} bytes)")

    if args.run:
        log("Launching QEMU...")
        subprocess.run([
            QEMU, "-display", "none", "-serial", "stdio",
            "-m", "4", "-drive", f"file={DISK_IMAGE},format=raw"
        ])

    return 0


if __name__ == "__main__":
    sys.exit(main())