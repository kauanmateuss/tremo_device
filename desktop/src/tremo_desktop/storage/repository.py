"""Uma conexão por thread. Inserção de evento e projeções na mesma transação."""
import hashlib
import json
import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from tremo_desktop.transport.protocol import InvalidMessage

SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions (
 device TEXT NOT NULL, session TEXT NOT NULL, patient_id TEXT,
 received_first TEXT NOT NULL, received_last TEXT NOT NULL, start_json TEXT,
 end_json TEXT, state INTEGER, state_seq INTEGER NOT NULL DEFAULT -1,
 end_seq INTEGER, PRIMARY KEY(device,session),
 FOREIGN KEY(patient_id) REFERENCES patients(patient_id));
CREATE TABLE IF NOT EXISTS events (
 device TEXT NOT NULL, session TEXT NOT NULL, seq INTEGER NOT NULL, type TEXT NOT NULL,
 t_us INTEGER NOT NULL, payload TEXT NOT NULL, received_at TEXT NOT NULL,
 PRIMARY KEY(device,session,seq), FOREIGN KEY(device,session) REFERENCES sessions(device,session));
CREATE TABLE IF NOT EXISTS trials (
 device TEXT NOT NULL, session TEXT NOT NULL, module TEXT NOT NULL, trial_index INTEGER NOT NULL,
 training INTEGER NOT NULL, outcome INTEGER NOT NULL, latency_us INTEGER, data TEXT NOT NULL,
 run INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(device,session,run,module,trial_index), FOREIGN KEY(device,session) REFERENCES sessions(device,session));
CREATE TABLE IF NOT EXISTS samples (
 device TEXT NOT NULL, session TEXT NOT NULL, sequence INTEGER NOT NULL, t_us INTEGER NOT NULL,
 sensor INTEGER NOT NULL, valid INTEGER NOT NULL, read_us INTEGER NOT NULL,
 ax INTEGER, ay INTEGER, az INTEGER, raw_gx INTEGER, raw_gy INTEGER, raw_gz INTEGER,
 gx INTEGER, gy INTEGER, gz INTEGER,
 run INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(device,session,run,sequence), FOREIGN KEY(device,session) REFERENCES sessions(device,session));
CREATE TABLE IF NOT EXISTS results (
 device TEXT NOT NULL, session TEXT NOT NULL, module TEXT NOT NULL, seq INTEGER NOT NULL, data TEXT NOT NULL,
 PRIMARY KEY(device,session,module), FOREIGN KEY(device,session) REFERENCES sessions(device,session));
CREATE TABLE IF NOT EXISTS rejected_messages (
 id INTEGER PRIMARY KEY, received_at TEXT NOT NULL, topic TEXT NOT NULL,
 payload_hash TEXT NOT NULL, reason TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS test_runs (
 run_id INTEGER PRIMARY KEY AUTOINCREMENT, device TEXT NOT NULL,
 session TEXT NOT NULL, module TEXT NOT NULL, run_number INTEGER NOT NULL,
 started_at TEXT, finished_at TEXT, state TEXT, data TEXT,
 FOREIGN KEY(device,session) REFERENCES sessions(device,session),
 UNIQUE(device,session,module,run_number));
CREATE TABLE IF NOT EXISTS devices (
 device_id TEXT PRIMARY KEY, firmware_version TEXT NOT NULL, state TEXT NOT NULL,
 created_at TEXT NOT NULL, updated_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS patients (
 patient_id TEXT PRIMARY KEY, name TEXT, created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS patient_devices (
 patient_id TEXT NOT NULL, device_id TEXT NOT NULL, associated_at TEXT NOT NULL,
 PRIMARY KEY(patient_id, device_id),
 FOREIGN KEY(patient_id) REFERENCES patients(patient_id) ON DELETE CASCADE,
 FOREIGN KEY(device_id) REFERENCES devices(device_id) ON DELETE CASCADE);
CREATE INDEX IF NOT EXISTS samples_time ON samples(device,session,sensor,t_us);

"""


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def cognitive_metrics(module, data, trials):
    measured = [t for t in trials if not t["training"]]
    counts = [sum(t["outcome"] == outcome for t in measured) for outcome in (0, 1, 2)]
    coherent = len(trials) == data["record_count"] and counts == [data["correct"], data["wrong"], data["omissions"]]
    latency_sum = sum(t["latency_us"] or 0 for t in measured if t["outcome"] == 0)
    coherent = coherent and latency_sum == data["valid_time_us"]
    total = data["correct"] + data["wrong"] + data["omissions"]
    metrics = dict(data, received_trials=len(trials), metrics_verified=coherent,
                   mean_valid_ms=data["valid_time_us"] / data["correct"] / 1000 if coherent and data["correct"] else None)
    if module == "rti":
        metrics.update(responses=data["correct"], anticipations=data["wrong"])
    else: metrics["accuracy_percent"] = 100 * data["correct"] / total if coherent and total else None
    return metrics


def utc_now():
    return datetime.now(timezone.utc).isoformat()


class Repository:
    def __init__(self, path, *, read_only=False):
        if read_only:
            uri = Path(path).resolve().as_uri() + "?mode=ro"
            self.db = sqlite3.connect(uri, uri=True, timeout=1)
        else:
            if str(path) != ":memory:":
                Path(path).parent.mkdir(parents=True, exist_ok=True)
            self.db = sqlite3.connect(path, timeout=5)
        self.db.row_factory = sqlite3.Row
        try:
            self.db.execute("PRAGMA foreign_keys=ON")
            version = self.db.execute("PRAGMA user_version").fetchone()[0]
            if version not in range(8) or (read_only and version == 0):
                raise RuntimeError("Versão do banco não suportada")
            if read_only and version < 7:
                archive = self.db
                self.db = sqlite3.connect(":memory:")
                try:
                    archive.backup(self.db)
                finally:
                    archive.close()
                self.db.row_factory = sqlite3.Row
                self._migrate()
                self.db.commit()
                self.db.execute("PRAGMA query_only=ON")
            if not read_only:
                self.db.execute("PRAGMA journal_mode=WAL")
                self.db.execute("PRAGMA synchronous=FULL")
                self.db.execute("BEGIN IMMEDIATE")
                if self.db.execute("PRAGMA user_version").fetchone()[0] < 7:
                    self._migrate()
                self.db.commit()
        except Exception:
            self.db.close()
            raise

    def _migrate(self):
        legacy = []
        for table in ("trials", "samples"):
            columns = [r[1] for r in self.db.execute(f"PRAGMA table_info({table})")]
            if columns and "run" not in columns:
                self.db.execute(f"ALTER TABLE {table} RENAME TO {table}_legacy")
                legacy.append((table, ",".join(columns)))
        self.db.execute("DROP INDEX IF EXISTS samples_time")
        for statement in SCHEMA.split(";"):
            if statement.strip():
                self.db.execute(statement)
        for table, columns in legacy:
            self.db.execute(f"INSERT INTO {table}({columns}) SELECT {columns} FROM {table}_legacy")
            self.db.execute(f"DROP TABLE {table}_legacy")
        if "patient_id" not in [r[1] for r in self.db.execute("PRAGMA table_info(sessions)")]:
            self.db.execute("ALTER TABLE sessions ADD COLUMN patient_id TEXT REFERENCES patients(patient_id)")
        self.db.execute("""INSERT OR IGNORE INTO devices
            SELECT device,'','identificado',min(received_first),max(received_last)
            FROM sessions GROUP BY device""")
        self.db.execute("""DELETE FROM patient_devices WHERE rowid NOT IN
            (SELECT rowid FROM (SELECT rowid,row_number() OVER
            (PARTITION BY device_id ORDER BY associated_at DESC,rowid DESC) AS pos
            FROM patient_devices) WHERE pos=1)""")
        self.db.execute("CREATE UNIQUE INDEX IF NOT EXISTS device_association ON patient_devices(device_id)")
        self.db.execute("""INSERT OR IGNORE INTO test_runs(device,session,module,run_number,finished_at,state,data)
            SELECT r.device,r.session,r.module,1,e.received_at,
            CASE json_extract(r.data,'$.interrupted') WHEN 1 THEN 'interrompida' ELSE 'concluida' END,r.data
            FROM results r JOIN events e USING(device,session,seq) WHERE r.module!='mpu'""")
        self.db.execute("PRAGMA user_version=7")

    def close(self):
        self.db.close()

    def reject(self, topic, payload, reason):
        with self.db:
            self.db.execute("INSERT INTO rejected_messages(received_at,topic,payload_hash,reason) VALUES(?,?,?,?)",
                            (utc_now(), topic[:256], hashlib.sha256(payload).hexdigest(), str(reason)[:256]))

    def ingest(self, event):
        e, d = event, event["data"]
        key = (e["device"], e["session"])
        payload, now = encoded(e), utc_now()
        try:
            with self.db:
                self.db.execute("BEGIN IMMEDIATE")
                previous = self.db.execute("SELECT payload FROM events WHERE device=? AND session=? AND seq=?", (*key, e["seq"])).fetchone()
                if previous:
                    if previous[0] != payload: raise InvalidMessage("mesma identidade com conteúdo diferente")
                    return False
                self.db.execute("INSERT OR IGNORE INTO devices VALUES(?,?,?,?,?)", (key[0], "", "identificado", now, now))
                run = e.get("run", 0)
                self.db.execute("INSERT INTO sessions(device,session,received_first,received_last) VALUES(?,?,?,?) ON CONFLICT(device,session) DO UPDATE SET received_last=excluded.received_last", (*key, now, now))
                self.db.execute("INSERT INTO events VALUES(?,?,?,?,?,?,?)", (*key, e["seq"], e["type"], e["t_us"], payload, now))
                if e["type"] == "start":
                    patient = d.get("patient_id")
                    if patient:
                        self.db.execute("INSERT OR IGNORE INTO patients(patient_id,created_at) VALUES(?,?)", (patient, now))
                    self.db.execute("UPDATE sessions SET start_json=?,patient_id=? WHERE device=? AND session=?", (encoded(d), patient or None, *key))
                if e["type"] in ("start", "state", "end"):
                    state = 1 if e["type"] == "start" else d["state"]
                    self.db.execute("UPDATE sessions SET state=?,state_seq=? WHERE device=? AND session=? AND state_seq<?", (state, e["seq"], *key, e["seq"]))
                if e["type"] == "end":
                    self.db.execute("UPDATE sessions SET end_json=?,end_seq=? WHERE device=? AND session=?", (encoded(d), e["seq"], *key))
                elif e["type"] == "trial":
                    # O início pode estar atrasado ou ausente. A identidade continua a mesma.
                    self.db.execute("""INSERT INTO test_runs(device,session,module,run_number,state)
                        VALUES(?,?,?,?,'em_andamento') ON CONFLICT(device,session,module,run_number)
                        DO NOTHING""", (*key, d["module"], run or 1))
                    latency = d["reaction_us"] if d["module"] == "rti" else (d["elapsed_us"] if d["outcome"] in (0, 1) else None)
                    self.db.execute("INSERT INTO trials VALUES(?,?,?,?,?,?,?,?,?)", (*key, d["module"], d["index"], d["training"], d["outcome"], latency, encoded(d), run))
                elif e["type"] == "test_start":
                    self.db.execute("""INSERT INTO test_runs(device,session,module,run_number,started_at,state)
                        VALUES(?,?,?,?,?,'em_andamento') ON CONFLICT(device,session,module,run_number)
                        DO UPDATE SET started_at=excluded.started_at""", (*key, d["module"], run, now))
                elif e["type"] == "result":
                    if d["module"] != "mpu":
                        self.create_test_run_record(key, d, run or 1, now)

                    self.db.execute("INSERT INTO results VALUES(?,?,?,?,?) ON CONFLICT(device,session,module) DO UPDATE SET seq=excluded.seq,data=excluded.data WHERE excluded.seq>results.seq", (*key, d["module"], e["seq"], encoded(d)))
                elif e["type"] == "samples":
                    for row in d["rows"]:
                        values = row[5:] if row[3] else [None] * 9
                        self.db.execute("INSERT INTO samples VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", (*key, *row[:5], *values, run))
            return True
        except sqlite3.IntegrityError as exc:
            raise InvalidMessage("conflito entre registros da sessão") from exc

    def create_test_run_record(self, key, data, number, now):
        self.db.execute("""INSERT INTO test_runs(device,session,module,run_number,finished_at,state,data)
            VALUES(?,?,?,?,?,?,?) ON CONFLICT(device,session,module,run_number)
            DO UPDATE SET finished_at=excluded.finished_at,state=excluded.state,data=excluded.data""", (*key, data["module"], number, now,
            "interrompida" if data["interrupted"] else "concluida", encoded(data)))

    def sessions(self, limit=100):
        return [dict(row) for row in self.db.execute(
            "SELECT device,session,received_first,received_last,state FROM sessions ORDER BY received_last DESC LIMIT ?", (limit,))]

    def overview(self, device, session):
        key = (device, session)
        row = self.db.execute("SELECT * FROM sessions WHERE device=? AND session=?", key).fetchone()
        if row is None: raise KeyError("Sessão não encontrada")
        config = json.loads(row["start_json"]) if row["start_json"] else None
        end = json.loads(row["end_json"]) if row["end_json"] else None
        seqs = [r[0] for r in self.db.execute("SELECT seq FROM events WHERE device=? AND session=? ORDER BY seq", key)]
        expected = row["end_seq"] + 1 if end else None
        gaps = (expected - sum(n < expected for n in seqs)) if expected is not None else None
        issues = []
        if not config: issues.append("início ausente")
        if not end: issues.append("encerramento ausente")
        if gaps: issues.append(f"{gaps} mensagens ausentes")
        if end and end["tx_dropped"]: issues.append(f'{end["tx_dropped"]} mensagens descartadas no dispositivo')
        if expected is not None and any(n >= expected for n in seqs): issues.append("mensagens após encerramento")
        trial_groups = {}
        for trial in self.db.execute("""SELECT module,run,training,outcome,latency_us FROM trials
                WHERE device=? AND session=?""", key):
            trial_groups.setdefault((trial["module"], trial["run"] or 1), []).append(trial)
        modules = {}
        for result in self.db.execute("SELECT module,data,seq FROM results WHERE device=? AND session=?", key):
            module, data = result["module"], json.loads(result["data"])
            run = self.db.execute("SELECT coalesce(json_extract(payload,'$.run'),0) FROM events WHERE device=? AND session=? AND seq=?", (*key, result["seq"])).fetchone()[0]
            if module == "mpu":
                received = [self.db.execute("SELECT count(*) FROM samples WHERE device=? AND session=? AND sensor=? AND valid=1 AND run=?", (*key, sensor, run)).fetchone()[0] for sensor in (1, 2)]
                if received != data["valid"]: issues.append("leituras MPU recebidas não correspondem ao resumo")
                if data["missed_slots"] or data["buffer_drops"] or any(data["errors"]): issues.append("falhas ou perdas na aquisição MPU")
                if not data["calibrated"]: issues.append("MPU sem calibração")
                nominal = (data["duration_ms"] + data["sample_ms"] - 1) // data["sample_ms"]
                modules[module] = dict(data, received_valid=received, nominal_samples_per_sensor=nominal,
                                       coverage=[min(1, n / nominal) if nominal else None for n in received])
                continue
            modules[module] = cognitive_metrics(module, data, trial_groups.get((module, run or 1), []))
            if not modules[module]["metrics_verified"]:
                issues.append(f"registros e resumo de {module.upper()} não conferem")
        if config and config["kind"] != 7 and end and end["state"] == 3:
            wanted = {0: ["mpu"], 1: ["mot"], 2: ["rti"], 3: ["prm"], 4: ["pal"]}.get(config["kind"])
            if wanted is None:
                wanted = [m for i, m in enumerate(("mot", "rti", "prm", "pal")) if config["tests_mask"] & (1 << i)]
                if config["kind"] == 6: wanted += ["mpu"]
            for module in wanted:
                if module not in modules: issues.append(f"resumo de {module.upper()} ausente")
        runs = self.test_runs_by_session(*key)
        for execution in runs:
            summary = json.loads(execution["data"] or "{}")
            required = {"record_count", "correct", "wrong", "omissions", "valid_time_us"}
            execution["metrics"] = None
            if required.issubset(summary):
                module = execution["module"]
                trials = trial_groups.get((module, execution["run_number"]), [])
                execution["metrics"] = cognitive_metrics(module, summary, trials)
                if not execution["metrics"]["metrics_verified"]:
                    issue = f"registros e resumo de {module.upper()} não conferem"
                    if issue not in issues:
                        issues.append(issue)
        if end and any(not r["finished_at"] for r in runs):
            issues.append("execução de teste sem resumo final")
        return dict(device=device, session=session, patient_id=row["patient_id"], runs=runs, state=row["state"], configuration=config, timing=end,
                    first_received_utc=row["received_first"], last_received_utc=row["received_last"],
                    quality=dict(complete=not issues, issues=issues, received_messages=len(seqs), expected_messages=expected),
                    modules=modules, protocol_label="Experimental adaptado; sem escores clínicos normativos")

    def chart_samples(self, device, session, sensor, after_us=0, limit=2000):
        rows = self.db.execute("SELECT sequence,t_us,valid,ax,ay,az,gx,gy,gz FROM samples WHERE device=? AND session=? AND sensor=? AND t_us>=? ORDER BY t_us LIMIT ?", (device, session, sensor, after_us, min(limit, 10000)))
        return [dict(sequence=r[0], t_us=r[1], valid=bool(r[2]),
                     acceleration_g=[v / 16384 if v is not None else None for v in r[3:6]],
                     angular_velocity_dps=[v / 131 if v is not None else None for v in r[6:9]]) for r in rows]

    def dashboard_sessions(self, query="", state=None, limit=200, patient=None, device=None, day=None):
        rows = self.db.execute("""
            SELECT device,session,patient_id,received_first,received_last,state,start_json,end_seq,
                (start_json IS NULL OR (end_seq IS NOT NULL AND (
                    json_extract(end_json,'$.tx_dropped')>0 OR
                    (SELECT count(*) FROM events e WHERE e.device=s.device AND e.session=s.session
                     AND e.seq<=s.end_seq)!=end_seq+1 OR EXISTS
                    (SELECT 1 FROM test_runs t WHERE t.device=s.device AND t.session=s.session
                     AND t.finished_at IS NULL)))) AS incomplete
            FROM sessions s
            WHERE (instr(device,:query)>0 OR instr(session,:query)>0)
              AND (:state IS NULL OR state=:state)
              AND (:patient IS NULL OR coalesce(patient_id,'')=:patient)
              AND (:device IS NULL OR device=:device)
              AND (:day IS NULL OR date(received_first,'localtime')=:day)
            ORDER BY received_first DESC,device,session LIMIT :limit""",
            dict(query=query.lower(), state=state, patient=patient, device=device, day=day, limit=limit))
        return [dict(row) for row in rows]

    def trial_details(self, device, session):
        return [dict(json.loads(row["data"]), run=row["run"] or 1, test_run_id=row["test_run_id"])
                for row in self.db.execute("""
            SELECT t.data,t.run,r.run_id AS test_run_id FROM trials t
            LEFT JOIN test_runs r ON r.device=t.device AND r.session=t.session AND r.module=t.module
                AND r.run_number=coalesce(nullif(t.run,0),1)
            WHERE t.device=? AND t.session=? ORDER BY t.run,t.module,t.trial_index""",
            (device, session))]

    def sample_counts(self, device, session):
        return {row[0]: row[1] for row in self.db.execute(
            "SELECT sensor,count(*) FROM samples WHERE device=? AND session=? GROUP BY sensor",
            (device, session))}

    # ----- gerenciamento de pacientes -----

    def patients(self):
        return [dict(row) for row in self.db.execute(
            "SELECT patient_id, name, created_at FROM patients ORDER BY patient_id")]

    def create_patient(self, patient_id, name=None):
        with self.db:
            self.db.execute(
                "INSERT INTO patients(patient_id, name, created_at) VALUES(?,?,?)",
                (patient_id, name, utc_now()))

    # ----- gerenciamento de dispositivos -----

    def devices(self):
        return [dict(row) for row in self.db.execute(
            "SELECT device_id, firmware_version, state, created_at, updated_at FROM devices ORDER BY device_id")]

    def create_device(self, device_id, firmware_version, state="ativo"):
        with self.db:
            self.db.execute(
                "INSERT INTO devices(device_id, firmware_version, state, created_at, updated_at) "
                "VALUES(?,?,?,?,?) "
                "ON CONFLICT(device_id) DO UPDATE SET firmware_version=excluded.firmware_version,"
                " state=excluded.state, updated_at=excluded.updated_at",
                (device_id, firmware_version, state, utc_now(), utc_now()))

    # ----- associacao paciente <-> dispositivo -----

    def patient_devices(self, patient_id):
        """Dispositivos associados a um paciente."""
        return [dict(row) for row in self.db.execute(
            "SELECT d.device_id, d.firmware_version, d.state, pd.associated_at "
            "FROM patient_devices pd JOIN devices d ON d.device_id = pd.device_id "
            "WHERE pd.patient_id=? ORDER BY d.device_id", (patient_id,))]

    def device_patients(self, device_id):
        """Pacientes associados a um dispositivo."""
        return [dict(row) for row in self.db.execute(
            "SELECT p.patient_id, p.name, pd.associated_at "
            "FROM patient_devices pd JOIN patients p ON p.patient_id = pd.patient_id "
            "WHERE pd.device_id=? ORDER BY p.patient_id", (device_id,))]

    def associate_patient_device(self, patient_id, device_id):
        with self.db:
            self.db.execute(
                "INSERT INTO patient_devices(patient_id, device_id, associated_at) VALUES(?,?,?) "
                "ON CONFLICT(device_id) DO UPDATE SET patient_id=excluded.patient_id, associated_at=excluded.associated_at WHERE patient_devices.patient_id!=excluded.patient_id",
                (patient_id, device_id, utc_now()))

    def disassociate_patient_device(self, patient_id, device_id):
        with self.db:
            self.db.execute(
                "DELETE FROM patient_devices WHERE patient_id=? AND device_id=?",
                (patient_id, device_id))

    # ----- sessoes e execucoes de teste -----

    def sessions_by_patient(self, patient_id):
        return [dict(row) for row in self.db.execute(
            "SELECT device, session, patient_id, received_first, received_last, state "
            "FROM sessions WHERE patient_id=? ORDER BY received_first DESC", (patient_id,))]

    def create_test_run(self, device, session, module, run_number, started_at=None,
                        finished_at=None, state="em_andamento", data=None):
        with self.db:
            self.db.execute(
                "INSERT INTO test_runs(device, session, module, run_number, started_at,"
                " finished_at, state, data) VALUES(?,?,?,?,?,?,?,?)",
                (device, session, module, run_number, started_at, finished_at, state,
                 encoded(data) if data is not None else None))

    def test_runs_by_session(self, device, session):
        return [dict(row) for row in self.db.execute(
            "SELECT run_id, run_id AS test_run_id, device, session, module, run_number, started_at, finished_at, state, data "
            "FROM test_runs WHERE device=? AND session=? ORDER BY run_number, module", (device, session))]
