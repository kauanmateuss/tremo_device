#!/usr/bin/env python3
"""Testa a lógica C com periféricos substituídos por funções de teste no PC."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
suites = {
    "controls": ["tests/test_controls.c", "src/controls.c"],
    "flow": ["tests/test_flow.c", "src/app.c", "src/assessment.c", "src/evaluation.c", "src/monitoring.c", "src/mot.c", "src/rti.c", "src/prm.c", "src/pal.c", "src/patterns.c"],
    "mot": ["tests/test_mot.c", "src/mot.c"],
    "rti": ["tests/test_rti.c", "src/rti.c"],
    "rti_input": ["tests/test_rti_input.c", "src/rti.c", "src/controls.c"],
    "prm": ["tests/test_prm.c", "src/prm.c", "src/patterns.c"],
    "pal": ["tests/test_pal.c", "src/pal.c", "src/patterns.c"],
    "monitoring": ["tests/test_monitoring.c", "src/monitoring.c"],
    "oled": ["tests/test_oled.c", "src/ssd1306_i2c.c"],
    "mpu": ["tests/test_mpu.c", "src/mpu6050_i2c.c"],
    "evaluation": ["tests/test_evaluation.c", "src/evaluation.c"],
    "communication": ["tests/test_communication.c", "src/communication.c"],
    "telemetry": ["tests/test_telemetry.c"],
    "session": ["tests/test_session.c"],
    "matrix": ["tests/test_matrix.c", "src/matrix.c"],
}
suites["flow_input"] = suites["flow"]
with tempfile.TemporaryDirectory(prefix="tremo-tests-") as directory:
    for name, sources in suites.items():
        sources = sources + ["src/session.c", "src/telemetry.c"]
        if name in ("flow", "flow_input"): sources += ["src/communication.c"]
        executable = str(Path(directory) / name)
        extra = ["-DTREMO_MQTT_ENABLED=1", "-I" + str(root / "tests/network_stubs")] if name == "communication" else []
        if name == "flow_input": extra += ["-DTEST_TASK_INPUT=1"]
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            *extra, "-I" + str(root / "tests/stubs"), "-I" + str(root / "include"),
            *[str(root / source) for source in sources], "-o", executable,
        ], check=True)
        result = subprocess.run([executable], capture_output=True, text=True)
        if result.returncode:
            print(result.stderr, end="")
            result.check_returncode()
        if name in ("flow", "flow_input"):
            assert "# MPU 0x69: falha I2C no reset" in result.stdout
            assert "# MPU 0x68:" not in result.stdout
            assert "# RESULTADO motivo=1" in result.stdout
            assert result.stdout.count("# RESULTADO") == 1
        if name == "mot":
            rows = [line.split(",") for line in result.stdout.splitlines() if line.startswith("MOT,")]
            assert len(rows) == 13
            assert sum(row[1] == "test" for row in rows) == 10
            assert sum(row[7] == "correct" for row in rows if row[1] == "test") == 8
            assert result.stdout.count("# MOT_END") == 1
        if name == "rti":
            rows = [line.split(",") for line in result.stdout.splitlines() if line.startswith("RTI,")]
            assert len(rows) == 13 and all(len(row) == 10 for row in rows)
            evaluated = [row for row in rows if row[1] == "test"]
            assert len(evaluated) == 10
            assert [row[2] for row in evaluated] == [str(i) for i in range(1, 11)]
            assert sum(row[3] == "response" for row in evaluated) == 8
            assert sum(row[3] == "anticipation" for row in evaluated) == 1
            assert sum(row[3] == "omission" for row in evaluated) == 1
            for row in rows:
                if row[3] == "response":
                    assert int(row[9]) == int(row[7]) - int(row[6])
                else:
                    assert row[9] == ""
                if row[3] == "anticipation":
                    assert row[6] == ""
                if row[3] == "omission":
                    assert row[7] == ""
            assert result.stdout.count("# RTI_END records=13") == 1
        if name == "prm":
            rows = [line.split(",") for line in result.stdout.splitlines() if line.startswith("PRM,")]
            assert len(rows) == 8 and all(len(row) == 10 for row in rows)
            evaluated = [row for row in rows if row[1] == "test"]
            assert len(evaluated) == 6
            assert [row[2] for row in evaluated] == [str(i) for i in range(1, 7)]
            assert [row[7] for row in evaluated].count("correct") == 4
            assert [row[7] for row in evaluated].count("wrong") == 1
            assert evaluated[-1][7] == "omission" and evaluated[-1][6] == ""
            assert sum(int(row[5]) for row in evaluated) == 3
            assert result.stdout.count("# PRM_END records=8") == 1
        if name == "pal":
            rows = [line.split(",") for line in result.stdout.splitlines() if line.startswith("PAL,")]
            assert len(rows) == 29 and all(len(row) == 11 for row in rows)
            evaluated = [row for row in rows if row[1] == "test"]
            assert len(evaluated) == 27
            assert [row[8] for row in evaluated].count("correct") == 21
            assert [row[8] for row in evaluated].count("wrong") == 5
            assert [row[8] for row in evaluated].count("omission") == 1
            for pairs in range(2, 5):
                for attempt in range(1, 4):
                    group = [row for row in evaluated if int(row[2]) == pairs and int(row[3]) == attempt]
                    assert [row[4] for row in group] == [str(i) for i in range(1, pairs + 1)]
            for row in evaluated:
                if row[8] == "omission":
                    assert row[7] == ""
                else:
                    assert (row[6] == row[7]) == (row[8] == "correct")
            assert result.stdout.count("# PAL_END records=29") == 1
        print(result.stdout, end="")
