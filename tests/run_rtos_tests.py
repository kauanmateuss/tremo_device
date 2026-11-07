#!/usr/bin/env python3
"""Exercita IPC com o kernel real no port POSIX, substituindo apenas o hardware."""
from pathlib import Path
import argparse
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--kernel", type=Path, default=root.parent / "FreeRTOS-Kernel")
args = parser.parse_args()
kernel = args.kernel.resolve()
port = kernel / "portable/ThirdParty/GCC/Posix"
with tempfile.TemporaryDirectory(prefix="tremo-rtos-tests-") as directory:
    executable = str(Path(directory) / "test_display_task")
    sources = [
        root / "tests/test_display_task.c", root / "src/display_task.c",
        *[kernel / name for name in ("tasks.c", "queue.c", "list.c", "timers.c", "event_groups.c")],
        kernel / "portable/MemMang/heap_4.c", port / "port.c", port / "utils/wait_for_event.c",
    ]
    subprocess.run([
        "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-pthread",
        "-I" + str(root / "tests/rtos_stubs"),
        "-I" + str(root / "tests/stubs"), "-I" + str(root / "include"),
        "-I" + str(kernel / "include"), "-I" + str(port),
        *map(str, sources), "-o", executable,
    ], check=True)
    subprocess.run([executable], check=True, timeout=15)
