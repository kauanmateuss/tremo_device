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
suites = {
    "display_task": ["src/display_task.c"],
    "mpu_task": ["src/mpu_task.c", "src/monitoring.c"],
    "input_task": ["src/input_task.c", "src/controls.c"],
}
with tempfile.TemporaryDirectory(prefix="tremo-rtos-tests-") as directory:
    for name, modules in suites.items():
        executable = str(Path(directory) / name)
        sources = [
            root / f"tests/test_{name}.c", *[root / module for module in modules],
            *[kernel / name for name in ("tasks.c", "queue.c", "list.c", "timers.c", "event_groups.c")],
            kernel / "portable/MemMang/heap_4.c", port / "port.c", port / "utils/wait_for_event.c",
        ]
        defines = ["-DTREMO_FREERTOS_ENABLED=1"] if name == "mpu_task" else []
        subprocess.run([
            "cc", *defines, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-pthread",
            "-I" + str(root / "tests/rtos_stubs"),
            "-I" + str(root / "tests/stubs"), "-I" + str(root / "include"),
            "-I" + str(kernel / "include"), "-I" + str(port),
            *map(str, sources), "-o", executable,
        ], check=True)
        subprocess.run([executable], check=True, timeout=15)
