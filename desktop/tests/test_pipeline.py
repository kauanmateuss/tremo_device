import json
from pathlib import Path
import sqlite3
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import Mock

from tremo_desktop.storage.repository import Repository
from tremo_desktop.transport.protocol import decode, InvalidMessage
from tremo_desktop.transport.mqtt import Collector

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from generate_mqtt_fixture import generate
DEVICE, SESSION = "0123456789abcdef", "0000000000000001"
TOPIC = f"tremo/v1/devices/{DEVICE}/events"


def event(kind="start", seq=0, data=None):
    return dict(v=1, device=DEVICE, session=SESSION, seq=seq, type=kind, t_us=1000,
                data=data if data is not None else dict(kind=1, duration_ms=30000, sample_ms=40,
                    tests_mask=15, tremor_first=True, protocol="tremo-adaptado-v1"))


def end(seq, state=3):
    return event("end", seq, dict(state=state, active_us=1000, paused_us=0, pauses=0, tx_dropped=0))


def trial(index, training=False, outcome=0, module="mot"):
    data = dict(module=module, index=index, training=training, outcome=outcome,
                onset_us=1000, elapsed_us=250000, target=[2, 2], selected=[2, 2])
    if module == "rti":
        data = dict(module=module, index=index, training=training, outcome=outcome,
                    hold_us=0, onset_us=1000000 if outcome != 1 else None,
                    release_us=1250000 if outcome == 0 else 500000 if outcome == 1 else None,
                    ended_us=1250000, reaction_us=250000 if outcome == 0 else None,
                    planned_wait_us=1000000)
    return event("trial", index + 1, data)


def result(seq, module="mot", records=3, correct=1, wrong=1):
    return event("result", seq, dict(module=module, interrupted=False, correct=correct,
        wrong=wrong, omissions=0, valid_time_us=correct * 250000, record_count=records,
        levels_completed=0, rounds_started=0, limit_reached=False))


class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.repo = Repository(":memory:")
        self.addCleanup(self.repo.close)

    def put(self, e):
        return self.repo.ingest(decode(TOPIC, json.dumps(e).encode()))

    def test_invalid_messages(self):
        for payload in (b"{}", b"[]", b'{"v":1,"v":1}', b"NaN", b"\xff", b'{"extra":1e309}', b'{"extra":' + b"9" * 5000 + b"}"):
            with self.assertRaises(InvalidMessage): decode(TOPIC, payload)
        for key, value in (("v", True), ("seq", -1), ("t_us", 1.5), ("type", "unknown")):
            bad = event(); bad[key] = value
            with self.assertRaises(InvalidMessage): decode(TOPIC, json.dumps(bad).encode())
        bad = event(); bad["data"]["sample_ms"] = 40.0
        with self.assertRaises(InvalidMessage): decode(TOPIC, json.dumps(bad).encode())
        with self.assertRaises(InvalidMessage): decode("wrong/topic", json.dumps(event()).encode())

    def test_deduplication_conflict_and_rollback(self):
        self.assertTrue(self.put(event())); self.assertFalse(self.put(event()))
        changed = event(); changed["t_us"] = 2000
        with self.assertRaises(InvalidMessage): self.put(changed)
        self.put(trial(0))
        repeated = trial(0); repeated["seq"] = 2
        with self.assertRaises(InvalidMessage): self.put(repeated)
        self.assertEqual(self.repo.db.execute("SELECT count(*) FROM events").fetchone()[0], 2)

    def test_metrics_exclude_training_and_detect_missing(self):
        for e in (event(), trial(0, True), trial(1), trial(2, outcome=1), result(4), end(5)):
            self.put(e)
        report = self.repo.overview(DEVICE, SESSION)
        self.assertTrue(report["quality"]["complete"])
        self.assertEqual(report["modules"]["mot"]["mean_valid_ms"], 250)
        self.assertEqual(report["modules"]["mot"]["accuracy_percent"], 50)
        self.repo.db.execute("DELETE FROM events WHERE seq=2")
        report = self.repo.overview(DEVICE, SESSION)
        self.assertFalse(report["quality"]["complete"])
        self.assertIn("1 mensagens ausentes", report["quality"]["issues"])

    def test_out_of_order_and_incoherent_result(self):
        self.put(end(3)); self.put(event()); self.put(result(2))
        report = self.repo.overview(DEVICE, SESSION)
        self.assertEqual(report["state"], 3)
        self.assertFalse(report["quality"]["complete"])
        self.assertIsNone(report["modules"]["mot"]["mean_valid_ms"])

    def test_rti_anticipations_are_not_response_times(self):
        start = event(); start["data"]["kind"] = 2
        for e in (start, trial(0, module="rti"), trial(1, outcome=1, module="rti"),
                  result(3, "rti", records=2), end(4)): self.put(e)
        report = self.repo.overview(DEVICE, SESSION)
        self.assertTrue(report["quality"]["complete"])
        metrics = report["modules"]["rti"]
        self.assertEqual(metrics["mean_valid_ms"], 250)
        self.assertEqual(metrics["anticipations"], 1)
        self.assertNotIn("accuracy_percent", metrics)

    def test_sample_units_and_invalid_readings(self):
        self.put(event("samples", 1, dict(module="mpu", rows=[
            [0, 1000, 1, 1, 30, 16384, 0, -16384, 131, 0, 0, 131, 0, 0],
            [1, 1001, 2, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0]])))
        first = self.repo.chart_samples(DEVICE, SESSION, 1)[0]
        self.assertEqual(first["acceleration_g"], [1, 0, -1])
        self.assertEqual(first["angular_velocity_dps"], [1, 0, 0])
        self.assertEqual(self.repo.chart_samples(DEVICE, SESSION, 2)[0]["acceleration_g"], [None]*3)

    def test_ack_only_after_commit_and_quarantine(self):
        client = Mock(); client.ack.return_value = 0
        collector = Collector(self.repo, client=client)
        message = SimpleNamespace(topic=TOPIC, payload=json.dumps(event()).encode(), qos=1, mid=7)
        client.ack.side_effect = lambda *_: 0 if self.repo.db.execute("SELECT count(*) FROM events").fetchone()[0] == 1 else self.fail("ACK antes de persistir")
        collector._message(client, None, message)
        self.assertEqual(client.ack.call_count, 1)
        message.payload = b"invalid"
        collector._message(client, None, message)
        self.assertEqual(self.repo.db.execute("SELECT count(*) FROM rejected_messages").fetchone()[0], 1)
        client.ack.reset_mock()
        collector.ingestion.repository = Mock()
        collector.ingestion.repository.ingest.side_effect = sqlite3.OperationalError("disk full")
        message.payload = json.dumps(event()).encode()
        with self.assertRaises(sqlite3.OperationalError): collector._message(client, None, message)
        client.ack.assert_not_called()

    def test_memory_tests_real_json(self):
        for module in ("prm", "pal"):
            for payload in generate(module):
                self.repo.ingest(decode(TOPIC, payload.encode()))
            session = self.repo.sessions()[0]
            report = self.repo.overview(session["device"], session["session"])
            self.assertTrue(report["quality"]["complete"], report["quality"])
            self.assertTrue(report["modules"][module]["metrics_verified"])
            self.assertEqual(report["modules"][module]["record_count"], 8 if module == "prm" else 29)

    def test_firmware_real_json(self):
        records = generate()
        for payload in records:
            e = decode(TOPIC, payload.encode())
            self.repo.ingest(e)
            self.assertFalse(self.repo.ingest(e))
        session = self.repo.sessions()[0]
        report = self.repo.overview(session["device"], session["session"])
        self.assertTrue(report["quality"]["complete"], report["quality"])
        self.assertEqual(report["modules"]["mpu"]["received_valid"], [750, 750])
        self.assertEqual(report["modules"]["mot"]["omissions"], 10)
        self.assertEqual(report["modules"]["rti"]["omissions"], 10)
        self.assertGreater(len(records), 750)


if __name__ == "__main__":
    unittest.main()
