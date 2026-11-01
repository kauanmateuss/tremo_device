#!/usr/bin/env python3
"""Escreve JSONL gerado pela FSM C com relógio e sensores simulados."""
from pathlib import Path
import subprocess
import tempfile


def generate(module="flow"):
    root = Path(__file__).resolve().parents[1]
    sources = ["tests/export_mqtt_fixture.c"] + [f"src/{name}.c" for name in
        ("app", "assessment", "evaluation", "monitoring", "mot", "rti", "prm", "pal", "patterns", "session", "telemetry")]
    if module in ("prm", "pal"):
        sources = [f"tests/export_{module}_fixture.c", f"src/{module}.c", "src/patterns.c", "src/session.c", "src/telemetry.c"]
    with tempfile.TemporaryDirectory(prefix="tremo-fixture-") as tmp:
        binary = str(Path(tmp) / "fixture")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(root / "tests/stubs"),
                        "-I" + str(root / "include"), *[str(root / s) for s in sources], "-o", binary], check=True)
        output = subprocess.check_output([binary], text=True)
    return [line for line in output.splitlines() if line.startswith('{"v":')]


if __name__ == "__main__":
    print("\n".join(generate()))
