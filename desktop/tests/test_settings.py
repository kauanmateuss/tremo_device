import csv
import json
import tempfile
import unittest
from contextlib import closing
from pathlib import Path

from tremo_desktop.services.demo import populate_demo
from tremo_desktop.services.ingestion import Ingestion
from tremo_desktop.storage.export import export_database
from tremo_desktop.storage.repository import Repository
from tremo_desktop.transport.protocol import decode_command, encode_command, topic_confirmations


class SettingsTests(unittest.TestCase):
    def test_backup_and_csv_include_committed_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "avaliacoes.db"
            populate_demo(path)
            with closing(Repository(path)) as repo:
                count = repo.db.execute("SELECT COUNT(*) FROM test_runs").fetchone()[0]
                self.assertGreater(count, 0)
                backup = path.with_name("backup.db")
                export_database(path, backup)
                with closing(Repository(backup, read_only=True)) as copy:
                    self.assertEqual(copy.db.execute("SELECT COUNT(*) FROM test_runs").fetchone()[0], count)
                    self.assertEqual(copy.db.execute("PRAGMA integrity_check").fetchone()[0], "ok")
                target = path.with_suffix(".csv")
                export_database(path, target, csv_results=True)
                with target.open(encoding="utf-8-sig", newline="") as stream:
                    rows = list(csv.DictReader(stream, delimiter=";"))
                self.assertEqual(len({r["test_run_id"] for r in rows}), count)
                self.assertTrue(any(r["result_json"] for r in rows))
                with self.assertRaises(ValueError):
                    export_database(path, path, csv_results=True)

    def test_mpu_confirmation_preserves_association(self):
        device, request = "0123456789abcdef", "1234567890abcdef"
        command = encode_command(device, "configure_mpu", dict(duration_ms=60000, sample_ms=20, request_id=request))
        self.assertEqual(list(command["data"]), ["request_id", "sample_ms", "duration_ms"])
        with closing(Repository(":memory:")) as repo:
            ingestion = Ingestion(repo)
            ingestion.process_confirmation(dict(device=device, confirmation="association_confirmed",
                                                status="ok", data=dict(patient_id="PAC-1")))
            payload = dict(v=2, device=device, confirmation="mpu_configured", status="ok", data=dict(request_id=request))
            ingestion.process_confirmation(decode_command(topic_confirmations(device), json.dumps(payload).encode()))
            self.assertEqual(repo.device_patients(device)[0]["patient_id"], "PAC-1")
