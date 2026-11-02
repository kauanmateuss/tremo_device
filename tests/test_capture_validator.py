"""Casos de integridade do relatório de captura, sem conexão serial."""
import importlib.util
from pathlib import Path
import tempfile

spec = importlib.util.spec_from_file_location("capture_validator", Path(__file__).resolve().parents[1] / "tools/validate_capture.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / "capture.csv"
    def check(rows):
        path.write_text(rows, encoding="utf-8")
        return module.analyze(path, 40)
    rows = "".join(f"MPU,abcd00000001,{i},{100000 + (i // 2) * 40000 + (i % 2) * 500},{1 + i % 2},1,500,0,0,1,0,0,0\n" for i in range(6))
    assert check(rows)["passed"]
    assert not check(rows + rows.splitlines()[0] + "\n")["passed"]
    assert not check(rows.replace(",140000,", ",170000,"))["passed"]
    assert not check("\n".join(rows.splitlines()[2:]))["passed"]
    assert not check(rows.replace(",0,0,1,", ",nan,0,1,"))["passed"]
    assert not check("MPU,abc,0,100\n")["passed"]
    assert not check("")["passed"]
    missing = "\n".join(line for i, line in enumerate(rows.splitlines()) if i != 2)
    assert not check(missing)["passed"]
print("PASS captura: continuidade, timestamps, campos truncados e sensores ausentes")
