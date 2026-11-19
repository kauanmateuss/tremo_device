import math
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

from tremo_desktop.services.dashboard import load_dashboard, plot_series
from tremo_desktop.services.demo import populate_demo
from tremo_desktop.storage.repository import Repository


class DashboardDataTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="tremo-dashboard-test-")
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "sessions.sqlite3"
        populate_demo(self.path)

    def test_missing_file_is_not_created(self):
        path = self.path.parent / "absent.sqlite3"
        result = load_dashboard(path)
        self.assertTrue(result["missing"])
        self.assertIsNone(result["report"])
        self.assertFalse(path.exists())

    def test_read_only_does_not_migrate_or_write(self):
        repository = Repository(self.path, read_only=True)
        try:
            with self.assertRaises(sqlite3.OperationalError):
                repository.db.execute("DELETE FROM sessions")
        finally:
            repository.close()
        other = self.path.parent / "unsupported.sqlite3"
        with sqlite3.connect(other) as db:
            db.execute("PRAGMA user_version=99")
        with self.assertRaisesRegex(RuntimeError, "Versão"):
            load_dashboard(other)
        with sqlite3.connect(other) as db:
            self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0], 99)

    def test_selection_filter_metrics_and_losses(self):
        data = load_dashboard(self.path)
        self.assertEqual(len(data["sessions"]), 3)
        report = data["report"]
        self.assertTrue(report["quality"]["complete"])
        self.assertEqual(report["modules"]["pal"]["levels_completed"], 3)
        self.assertEqual(report["modules"]["mot"]["accuracy_percent"], 80)
        selected = (report["device"], "0000000000000002")
        interrupted = load_dashboard(self.path, selected, state=4)
        self.assertEqual(interrupted["selected"], selected)
        self.assertFalse(interrupted["report"]["quality"]["complete"])
        self.assertEqual(interrupted["sample_counts"], {1: 230, 2: 230})
        self.assertIn("20 mensagens ausentes", interrupted["report"]["quality"]["issues"])
        self.assertIsNone(load_dashboard(self.path, selected, query="not-found")["selected"])
        self.assertEqual(len(load_dashboard(self.path, query="0000000000000001")["sessions"]), 1)
        running = load_dashboard(self.path, state=1)
        self.assertIsNone(running["report"]["timing"])

    def test_snapshot_during_concurrent_write(self):
        original = Repository.overview
        def write_then_read(repository, device, session):
            with sqlite3.connect(self.path) as writer:
                writer.execute("UPDATE sessions SET state=5 WHERE device=? AND session=?", (device, session))
            return original(repository, device, session)
        with patch.object(Repository, "overview", write_then_read):
            snapshot = load_dashboard(self.path)
        self.assertEqual(snapshot["sessions"][0]["state"], 3)
        self.assertEqual(snapshot["report"]["state"], 3)
        self.assertEqual(load_dashboard(self.path)["report"]["state"], 5)

    def test_overview_follows_current_association(self):
        overview = load_dashboard(self.path)["latest"]
        self.assertEqual(set(overview["curves"]), {1, 2})
        self.assertTrue(overview["last_received"])
        repo = Repository(self.path)
        try:
            repo.create_patient("PAC-NOVO")
            repo.associate_patient_device("PAC-NOVO", overview["device"])
        finally:
            repo.close()
        overview = load_dashboard(self.path)["latest"]
        self.assertEqual(overview["patient"], "PAC-NOVO")
        self.assertEqual(overview["counts"], (0, 0))
        self.assertFalse(overview["report"])

    def test_gaps_invalid_values_and_shared_time(self):
        def sample(sequence, timestamp, valid=True):
            return dict(sequence=sequence, t_us=timestamp, valid=valid,
                        acceleration_g=[1., 2., 3.] if valid else [None]*3)
        rows = [sample(0, 1000000), sample(2, 1040000, False), sample(6, 1120000), sample(8, 2000000)]
        times, axes = plot_series(rows, "acceleration_g", 999800, 40)
        self.assertAlmostEqual(times[0], .0002)
        self.assertEqual(len(times), 6)
        self.assertEqual(sum(math.isnan(x) for x in axes[0]), 3)
        self.assertEqual(axes[0][0], 1.)


if __name__ == "__main__":
    unittest.main()
