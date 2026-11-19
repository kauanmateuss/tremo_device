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
    "rtos_integration": [
        "src/session_task.c", "src/input_task.c", "src/display_task.c", "src/mpu_task.c",
        "src/app.c", "src/assessment.c", "src/session.c", "src/evaluation.c",
        "src/controls.c", "src/monitoring.c", "src/mot.c", "src/rti.c",
        "src/prm.c", "src/pal.c", "src/patterns.c", "src/telemetry.c",
        "src/communication.c", "src/patient_assoc.c", "tests/rtos_stubs/network_fixture.c",
    ],
    "network_task": ["src/communication.c", "src/patient_assoc.c", "src/telemetry.c", "src/session.c", "tests/rtos_stubs/network_fixture.c"],
    "display_task": ["src/display_task.c"],
    "mpu_task": ["src/mpu_task.c", "src/monitoring.c", "src/mot.c", "src/prm.c", "src/pal.c", "src/patterns.c"],
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
        defines = ["-DTREMO_FREERTOS_ENABLED=1"] if name in ("mpu_task", "network_task", "rtos_integration") else []
        if name in ("network_task", "rtos_integration"):
            defines += ["-DTREMO_MQTT_ENABLED=1", "-I" + str(root / "tests/network_stubs")]
        subprocess.run([
            "cc", *defines, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-pthread",
            "-I" + str(root / "tests/rtos_stubs"),
            "-I" + str(root / "tests/stubs"), "-I" + str(root / "include"),
            "-I" + str(kernel / "include"), "-I" + str(port),
            *map(str, sources), "-o", executable,
        ], check=True)
        subprocess.run([executable], check=True, timeout=30)
