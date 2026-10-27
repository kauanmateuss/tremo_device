#!/usr/bin/env python3
"""Testa a lógica C com periféricos substituídos por funções de teste no PC."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
suites = {
    "controls": ["tests/test_controls.c", "src/controls.c"],
    "flow": ["tests/test_flow.c", "src/app.c", "src/monitoring.c", "src/mot.c"],
    "mot": ["tests/test_mot.c", "src/mot.c"],
    "matrix": ["tests/test_matrix.c", "src/matrix.c"],
}
with tempfile.TemporaryDirectory(prefix="tremo-tests-") as directory:
    for name, sources in suites.items():
        executable = str(Path(directory) / name)
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I" + str(root / "tests/stubs"), "-I" + str(root / "include"),
            *[str(root / source) for source in sources], "-o", executable,
        ], check=True)
        result = subprocess.run([executable], capture_output=True, text=True)
        if result.returncode:
            print(result.stderr, end="")
            result.check_returncode()
        if name == "flow":
            assert "# RESULTADO motivo=1" in result.stdout
            assert result.stdout.count("# RESULTADO") == 1
        if name == "mot":
            rows = [line.split(",") for line in result.stdout.splitlines() if line.startswith("MOT,")]
            assert len(rows) == 13
            assert sum(row[1] == "test" for row in rows) == 10
            assert sum(row[7] == "correct" for row in rows if row[1] == "test") == 8
            assert result.stdout.count("# MOT_END") == 1
        print(result.stdout, end="")
