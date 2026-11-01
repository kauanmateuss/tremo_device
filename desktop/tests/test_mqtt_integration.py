"""Teste opt-in com Mosquitto isolado; não altera o broker do notebook."""
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest

from test_pipeline import generate, TOPIC, event, end


def until(check, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if check(): return
        time.sleep(0.05)
    raise AssertionError("Tempo esgotado aguardando condição de integração")


@unittest.skipUnless(os.getenv("TREMO_TEST_MQTT") == "1", "defina TREMO_TEST_MQTT=1 para usar Mosquitto local")
class BrokerIntegration(unittest.TestCase):
    def test_delivery_duplicates_offline_and_quarantine(self):
        import paho.mqtt.client as mqtt
        # Neste perfil, o broker não pode receber SIGTERM do executor do teste.
        # Exigir broker administrado externamente evita deixar processos órfãos.
        if not os.getenv("TREMO_TEST_PORT") and Path("/etc/apparmor.d/mosquitto").exists():
            self.skipTest("perfil AppArmor presente: use TREMO_TEST_PORT com um broker de testes local")
        with tempfile.TemporaryDirectory(prefix="tremo-mosquitto-") as directory:
            tmp = Path(directory)
            tmp.chmod(0o755)  # Mosquitto pode reduzir privilégios quando iniciado como root.
            external_port = os.getenv("TREMO_TEST_PORT")
            if external_port:
                port = int(external_port)
            else:
                with socket.socket() as probe:
                    probe.bind(("127.0.0.1", 0)); port = probe.getsockname()[1]
                broker_log = (tmp / "broker.log").open("w")
                broker = subprocess.Popen([shutil.which("mosquitto") or "/usr/sbin/mosquitto", "-p", str(port)], stdout=broker_log, stderr=broker_log)
                self.addCleanup(broker_log.close)
                self.addCleanup(lambda: broker.wait(timeout=5))
                self.addCleanup(broker.terminate)

            def listening():
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=.2): return True
                except OSError:
                    return False
            until(listening)
            db = tmp / "data.sqlite3"
            processes = []

            def start_collector():
                logpath = tmp / f"collector-{len(processes)}.log"
                log = logpath.open("w")
                proc = subprocess.Popen([sys.executable, "-m", "tremo_desktop", "--db", str(db),
                                         "collect", "--host", "127.0.0.1", "--port", str(port),
                                         "--client-id", "tremo-integration-collector"], stdout=log, stderr=log)
                processes.append(proc)
                self.addCleanup(log.close)
                def stop():
                    if proc.poll() is None: proc.send_signal(signal.SIGINT)
                    proc.wait(timeout=5)
                self.addCleanup(stop)
                def ready():
                    if proc.poll() is not None: self.fail(logpath.read_text())
                    return "Coletor pronto" in logpath.read_text()
                until(ready)
                return proc

            first = start_collector()
            publisher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="tremo-integration-publisher")
            publisher.connect("127.0.0.1", port, 30); publisher.loop_start()
            self.addCleanup(publisher.loop_stop)
            self.addCleanup(publisher.disconnect)
            def publish(payload):
                msg = publisher.publish(TOPIC, payload, qos=1)
                msg.wait_for_publish(timeout=10)
                self.assertTrue(msg.is_published())
            records = generate()
            for payload in records: publish(payload)
            # Reenvio de aplicação: a chave (placa, sessão, sequência) elimina duplicação.
            for payload in records[:20]: publish(payload)
            def count(table):
                with sqlite3.connect(db) as conn:
                    return conn.execute(f"SELECT count(*) FROM {table}").fetchone()[0]
            until(lambda: count("events") == len(records))
            self.assertEqual(count("samples"), 1500)
            first.send_signal(signal.SIGINT); self.assertEqual(first.wait(timeout=5), 0)
            offline_start = event(); offline_start["session"] = "0000000000000002"
            offline_end = end(1, 4); offline_end["session"] = offline_start["session"]
            publish(json.dumps(offline_start)); publish(json.dumps(offline_end)); publish("invalid JSON")
            self.assertEqual(count("events"), len(records))
            start_collector()
            until(lambda: count("events") == len(records) + 2 and count("rejected_messages") == 1)
            self.assertEqual(count("sessions"), 2)
            from tremo_desktop.storage.repository import Repository
            repository = Repository(db)
            try:
                session = json.loads(records[0])
                self.assertTrue(repository.overview(session["device"], session["session"])["quality"]["complete"])
            finally:
                repository.close()


if __name__ == "__main__":
    unittest.main()
