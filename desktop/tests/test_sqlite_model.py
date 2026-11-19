"""Validacao do novo modelo SQLite: pacientes, dispositivos, associacao,
sessoes vinculadas e execucoes de teste (test_runs) por sessao."""
import sqlite3
import tempfile
import unittest
from pathlib import Path

from tremo_desktop.storage.repository import Repository


class SqliteModelTests(unittest.TestCase):
    def setUp(self):
        self.repo = Repository(":memory:")
        self.addCleanup(self.repo.close)

    def test_crud_paciente_dispositivo(self):
        self.repo.create_patient("paciente-01", "Maria Silva")
        self.repo.create_device("AA:BB:CC:DD:EE:FF", "1.2.3", "ativo")

        pacientes = self.repo.pacientes() if hasattr(self.repo, "pacientes") else self.repo.patients()
        self.assertEqual(len(pacientes), 1)
        self.assertEqual(pacientes[0]["patient_id"], "paciente-01")
        self.assertEqual(pacientes[0]["name"], "Maria Silva")

        dispositivos = self.repo.devices()
        self.assertEqual(len(dispositivos), 1)
        self.assertEqual(dispositivos[0]["device_id"], "AA:BB:CC:DD:EE:FF")
        self.assertEqual(dispositivos[0]["firmware_version"], "1.2.3")
        self.assertEqual(dispositivos[0]["state"], "ativo")

    def test_associar_desassociar_paciente_dispositivo(self):
        self.repo.create_patient("paciente-01", "Maria Silva")
        self.repo.create_device("AA:BB:CC:DD:EE:FF", "1.2.3", "ativo")
        self.repo.create_device("11:22:33:44:55:66", "2.0.0", "inativo")

        # Associacao
        self.repo.associate_patient_device("paciente-01", "AA:BB:CC:DD:EE:FF")
        devs = self.repo.patient_devices("paciente-01")
        self.assertEqual(len(devs), 1)
        self.assertEqual(devs[0]["device_id"], "AA:BB:CC:DD:EE:FF")

        # Mesmo dispositivo pode ter varios pacientes? Aqui so um, mas consulta pelo device
        paci = self.repo.device_patients("AA:BB:CC:DD:EE:FF")
        self.assertEqual(len(paci), 1)
        self.assertEqual(paci[0]["patient_id"], "paciente-01")

        # Duplicacao nao replica (ON CONFLICT atualiza)
        self.repo.associate_patient_device("paciente-01", "AA:BB:CC:DD:EE:FF")
        self.assertEqual(len(self.repo.patient_devices("paciente-01")), 1)

        # Desassociacao
        self.repo.disassociate_patient_device("paciente-01", "AA:BB:CC:DD:EE:FF")
        self.assertEqual(len(self.repo.patient_devices("paciente-01")), 0)
        self.assertEqual(len(self.repo.device_patients("AA:BB:CC:DD:EE:FF")), 0)

    def test_sessao_vinculada_a_paciente_e_dispositivo(self):
        self.repo.create_patient("paciente-01", "Maria Silva")
        self.repo.create_device("AA:BB:CC:DD:EE:FF", "1.2.3", "ativo")
        self.repo.associate_patient_device("paciente-01", "AA:BB:CC:DD:EE:FF")

        # Ingest cria sessao; vincula o patient_id apos
        self.repo.db.execute(
            "INSERT INTO sessions(device, session, patient_id, received_first, received_last, state) "
            "VALUES(?,?,?,?,?,?)",
            ("AA:BB:CC:DD:EE:FF", "sessao-001", "paciente-01", "2026-01-01T00:00:00Z",
             "2026-01-01T00:30:00Z", 3))
        self.repo.db.commit()

        sessoes = self.repo.sessions_by_patient("paciente-01")
        self.assertEqual(len(sessoes), 1)
        self.assertEqual(sessoes[0]["device"], "AA:BB:CC:DD:EE:FF")
        self.assertEqual(sessoes[0]["session"], "sessao-001")
        self.assertEqual(sessoes[0]["patient_id"], "paciente-01")
        self.assertEqual(sessoes[0]["state"], 3)

    def test_test_runs_multiplas_execucoes_por_sessao(self):
        self.repo.create_patient("paciente-01", "Maria Silva")
        self.repo.create_device("AA:BB:CC:DD:EE:FF", "1.2.3", "ativo")
        self.repo.db.execute(
            "INSERT INTO sessions(device, session, patient_id, received_first, received_last, state) "
            "VALUES(?,?,?,?,?,?)",
            ("AA:BB:CC:DD:EE:FF", "sessao-001", "paciente-01", "2026-01-01T00:00:00Z",
             "2026-01-01T00:30:00Z", 3))
        self.repo.db.commit()

        # Duas execucoes de testes cognitivos na mesma sessao
        self.repo.create_test_run("AA:BB:CC:DD:EE:FF", "sessao-001", "mot", 1,
                                  "2026-01-01T00:05:00Z", "2026-01-01T00:10:00Z", "concluida",
                                  dict(correct=8, wrong=1))
        self.repo.create_test_run("AA:BB:CC:DD:EE:FF", "sessao-001", "rti", 1,
                                  "2026-01-01T00:15:00Z", "2026-01-01T00:20:00Z", "concluida",
                                  dict(mean_valid_ms=240))
        self.repo.create_test_run("AA:BB:CC:DD:EE:FF", "sessao-001", "mot", 2,
                                  "2026-01-01T00:22:00Z", "2026-01-01T00:27:00Z", "concluida",
                                  dict(correct=9, wrong=0))

        runs = self.repo.test_runs_by_session("AA:BB:CC:DD:EE:FF", "sessao-001")
        self.assertEqual(len(runs), 3)
        modulos = sorted(r["module"] for r in runs)
        self.assertEqual(modulos, ["mot", "mot", "rti"])
        numeros = sorted(r["run_number"] for r in runs)
        self.assertEqual(numeros, [1, 1, 2])

    def test_migracao_banco_versao_5_para_7(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "banco.sqlite3"
            # Banco na versao 5 (sessoes ja tem patient_id, sem test_runs)
            with sqlite3.connect(path) as db:
                db.execute("PRAGMA user_version=5")
                db.execute(
                    "CREATE TABLE sessions ("
                    " device TEXT NOT NULL, session TEXT NOT NULL, patient_id TEXT,"
                    " received_first TEXT NOT NULL, received_last TEXT NOT NULL,"
                    " start_json TEXT, end_json TEXT, state INTEGER,"
                    " state_seq INTEGER NOT NULL DEFAULT -1, end_seq INTEGER,"
                    " PRIMARY KEY(device,session),"
                    " FOREIGN KEY(patient_id) REFERENCES patients(patient_id))")
                db.execute("CREATE TABLE patients (patient_id TEXT PRIMARY KEY, name TEXT, created_at TEXT NOT NULL)")
                db.execute("CREATE TABLE devices (device_id TEXT PRIMARY KEY, firmware_version TEXT NOT NULL,"
                           " state TEXT NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)")
                db.execute("CREATE TABLE patient_devices (patient_id TEXT NOT NULL, device_id TEXT NOT NULL,"
                           " associated_at TEXT NOT NULL, PRIMARY KEY(patient_id, device_id),"
                           " FOREIGN KEY(patient_id) REFERENCES patients(patient_id) ON DELETE CASCADE,"
                           " FOREIGN KEY(device_id) REFERENCES devices(device_id) ON DELETE CASCADE)")
                db.execute("CREATE TABLE rejected_messages (id INTEGER PRIMARY KEY, received_at TEXT NOT NULL,"
                           " topic TEXT NOT NULL, payload_hash TEXT NOT NULL, reason TEXT NOT NULL)")
                db.commit()

            repo = Repository(path)
            try:
                # A migracao cria a tabela test_runs
                self.assertEqual(repo.db.execute("PRAGMA user_version").fetchone()[0], 7)
                tabela = repo.db.execute(
                    "SELECT name FROM sqlite_master WHERE type='table' AND name='test_runs'").fetchone()
                self.assertIsNotNone(tabela)
                # Colunas esperadas
                cols = {row[1] for row in repo.db.execute("PRAGMA table_info(test_runs)")}
                self.assertTrue({"run_id", "device", "session", "module", "run_number"}.issubset(cols))
            finally:
                repo.close()
