"""Uma conexão por thread. Inserção de evento e projeções na mesma transação."""
import hashlib
import json
import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from tremo_desktop.transport.protocol import InvalidMessage

SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions (
 device TEXT NOT NULL, session TEXT NOT NULL, received_first TEXT NOT NULL,
 received_last TEXT NOT NULL, start_json TEXT, end_json TEXT, state INTEGER,
 state_seq INTEGER NOT NULL DEFAULT -1, end_seq INTEGER,
 PRIMARY KEY(device,session));
CREATE TABLE IF NOT EXISTS events (
 device TEXT NOT NULL, session TEXT NOT NULL, seq INTEGER NOT NULL, type TEXT NOT NULL,
 t_us INTEGER NOT NULL, payload TEXT NOT NULL, received_at TEXT NOT NULL,
 PRIMARY KEY(device,session,seq), FOREIGN KEY(device,session) REFERENCES sessions(device,session));
CREATE TABLE IF NOT EXISTS trials (
 device TEXT NOT NULL, session TEXT NOT NULL, module TEXT NOT NULL, trial_index INTEGER NOT NULL,
 training INTEGER NOT NULL, outcome INTEGER NOT NULL, latency_us INTEGER, data TEXT NOT NULL,
 PRIMARY KEY(device,session,module,trial_index), FOREIGN KEY(device,session) REFERENCES sessions(device,session));
CREATE TABLE IF NOT EXISTS samples (
 device TEXT NOT NULL, session TEXT NOT NULL, sequence INTEGER NOT NULL, t_us INTEGER NOT NULL,
 sensor INTEGER NOT NULL, valid INTEGER NOT NULL, read_us INTEGER NOT NULL,
 ax INTEGER, ay INTEGER, az INTEGER, raw_gx INTEGER, raw_gy INTEGER, raw_gz INTEGER,
 gx INTEGER, gy INTEGER, gz INTEGER,
 PRIMARY KEY(device,session,sequence), FOREIGN KEY(device,session) REFERENCES sessions(device,session));
CREATE TABLE IF NOT EXISTS results (
 device TEXT NOT NULL, session TEXT NOT NULL, module TEXT NOT NULL, seq INTEGER NOT NULL, data TEXT NOT NULL,
 PRIMARY KEY(device,session,module), FOREIGN KEY(device,session) REFERENCES sessions(device,session));
CREATE TABLE IF NOT EXISTS rejected_messages (
 id INTEGER PRIMARY KEY, received_at TEXT NOT NULL, topic TEXT NOT NULL,
 payload_hash TEXT NOT NULL, reason TEXT NOT NULL);
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
PRAGMA user_version=4;
"""


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


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
            if version not in ((1, 2, 3, 4) if read_only else (0, 1, 2, 3, 4)):
                raise RuntimeError("Versão do banco não suportada")
            if not read_only:
                self.db.execute("PRAGMA journal_mode=WAL")
                self.db.execute("PRAGMA synchronous=FULL")
                if version == 0:
                    self.db.executescript("BEGIN;" + SCHEMA + "COMMIT;")
                elif version == 1:
                    self.db.executescript(
                        "BEGIN;"
                        "CREATE TABLE IF NOT EXISTS devices ("
                        " device_id TEXT PRIMARY KEY, firmware_version TEXT NOT NULL, state TEXT NOT NULL,"
                        " created_at TEXT NOT NULL, updated_at TEXT NOT NULL);"
                        "COMMIT;"
                    )
                elif version == 2:
                    self.db.executescript(
                        "BEGIN;"
                        "CREATE TABLE IF NOT EXISTS patients ("
                        " patient_id TEXT PRIMARY KEY, name TEXT, created_at TEXT NOT NULL);"
                        "COMMIT;"
                    )
                elif version == 3:
                    self.db.executescript(
                        "BEGIN;"
                        "CREATE TABLE IF NOT EXISTS patient_devices ("
                        " patient_id TEXT NOT NULL, device_id TEXT NOT NULL, associated_at TEXT NOT NULL,"
                        " PRIMARY KEY(patient_id, device_id),"
                        " FOREIGN KEY(patient_id) REFERENCES patients(patient_id) ON DELETE CASCADE,"
                        " FOREIGN KEY(device_id) REFERENCES devices(device_id) ON DELETE CASCADE);"
                        "COMMIT;"
                    )
        except Exception:
            self.db.close()
            raise

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
                previous = self.db.execute("SELECT payload FROM events WHERE device=? AND session=? AND seq=?", (*key, e["seq"])).fetchone()
                if previous:
                    if previous[0] != payload: raise InvalidMessage("mesma identidade com conteúdo diferente")
                    return False
                self.db.execute("INSERT INTO sessions(device,session,received_first,received_last) VALUES(?,?,?,?) ON CONFLICT(device,session) DO UPDATE SET received_last=excluded.received_last", (*key, now, now))
                self.db.execute("INSERT INTO events VALUES(?,?,?,?,?,?,?)", (*key, e["seq"], e["type"], e["t_us"], payload, now))
                if e["type"] == "start":
                    self.db.execute("UPDATE sessions SET start_json=? WHERE device=? AND session=?", (encoded(d), *key))
                if e["type"] in ("start", "state", "end"):
                    state = 1 if e["type"] == "start" else d["state"]
                    self.db.execute("UPDATE sessions SET state=?,state_seq=? WHERE device=? AND session=? AND state_seq<?", (state, e["seq"], *key, e["seq"]))
                if e["type"] == "end":
                    self.db.execute("UPDATE sessions SET end_json=?,end_seq=? WHERE device=? AND session=?", (encoded(d), e["seq"], *key))
                elif e["type"] == "trial":
                    latency = d["reaction_us"] if d["module"] == "rti" else (d["elapsed_us"] if d["outcome"] in (0, 1) else None)
                    self.db.execute("INSERT INTO trials VALUES(?,?,?,?,?,?,?,?)", (*key, d["module"], d["index"], d["training"], d["outcome"], latency, encoded(d)))
                elif e["type"] == "result":
                    self.db.execute("INSERT INTO results VALUES(?,?,?,?,?) ON CONFLICT(device,session,module) DO UPDATE SET seq=excluded.seq,data=excluded.data WHERE excluded.seq>results.seq", (*key, d["module"], e["seq"], encoded(d)))
                elif e["type"] == "samples":
                    for row in d["rows"]:
                        values = row[5:] if row[3] else [None] * 9
                        self.db.execute("INSERT INTO samples VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", (*key, *row[:5], *values))
            return True
        except sqlite3.IntegrityError as exc:
            raise InvalidMessage("conflito entre registros da sessão") from exc

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
        modules = {}
        for result in self.db.execute("SELECT module,data FROM results WHERE device=? AND session=?", key):
            module, data = result["module"], json.loads(result["data"])
            if module == "mpu":
                received = [self.db.execute("SELECT count(*) FROM samples WHERE device=? AND session=? AND sensor=? AND valid=1", (*key, sensor)).fetchone()[0] for sensor in (1, 2)]
                if received != data["valid"]: issues.append("leituras MPU recebidas não correspondem ao resumo")
                if data["missed_slots"] or data["buffer_drops"] or any(data["errors"]): issues.append("falhas ou perdas na aquisição MPU")
                if not data["calibrated"]: issues.append("MPU sem calibração")
                nominal = (data["duration_ms"] + data["sample_ms"] - 1) // data["sample_ms"]
                modules[module] = dict(data, received_valid=received, nominal_samples_per_sensor=nominal,
                                       coverage=[min(1, n / nominal) if nominal else None for n in received])
                continue
            trials = self.db.execute("SELECT training,outcome,latency_us FROM trials WHERE device=? AND session=? AND module=?", (*key, module)).fetchall()
            measured = [t for t in trials if not t["training"]]
            counts = [sum(t["outcome"] == outcome for t in measured) for outcome in (0, 1, 2)]
            coherent = len(trials) == data["record_count"] and counts == [data["correct"], data["wrong"], data["omissions"]]
            latency_sum = sum(t["latency_us"] or 0 for t in measured if t["outcome"] == 0)
            coherent = coherent and latency_sum == data["valid_time_us"]
            if not coherent: issues.append(f"registros e resumo de {module.upper()} não conferem")
            total = data["correct"] + data["wrong"] + data["omissions"]
            metrics = dict(data, received_trials=len(trials), metrics_verified=coherent,
                           mean_valid_ms=data["valid_time_us"] / data["correct"] / 1000 if coherent and data["correct"] else None)
            if module == "rti":
                metrics.update(responses=data["correct"], anticipations=data["wrong"])
            else: metrics["accuracy_percent"] = 100 * data["correct"] / total if coherent and total else None
            modules[module] = metrics
        if config and end and end["state"] == 3:
            wanted = {0: ["mpu"], 1: ["mot"], 2: ["rti"], 3: ["prm"], 4: ["pal"]}.get(config["kind"])
            if wanted is None:
                wanted = [m for i, m in enumerate(("mot", "rti", "prm", "pal")) if config["tests_mask"] & (1 << i)]
                if config["kind"] == 6: wanted += ["mpu"]
            for module in wanted:
                if module not in modules: issues.append(f"resumo de {module.upper()} ausente")
        return dict(device=device, session=session, state=row["state"], configuration=config, timing=end,
                    first_received_utc=row["received_first"], last_received_utc=row["received_last"],
                    quality=dict(complete=not issues, issues=issues, received_messages=len(seqs), expected_messages=expected),
                    modules=modules, protocol_label="Experimental adaptado; sem escores clínicos normativos")

    def chart_samples(self, device, session, sensor, after_us=0, limit=2000):
        rows = self.db.execute("SELECT sequence,t_us,valid,ax,ay,az,gx,gy,gz FROM samples WHERE device=? AND session=? AND sensor=? AND t_us>=? ORDER BY t_us LIMIT ?", (device, session, sensor, after_us, min(limit, 10000)))
        return [dict(sequence=r[0], t_us=r[1], valid=bool(r[2]),
                     acceleration_g=[v / 16384 if v is not None else None for v in r[3:6]],
                     angular_velocity_dps=[v / 131 if v is not None else None for v in r[6:9]]) for r in rows]

    def dashboard_sessions(self, query="", state=None, limit=200):
        # A pesquisa é feita no banco inteiro antes de limitar a lista visível.
        rows = self.db.execute(
            "SELECT device,session,received_first,received_last,state,start_json FROM sessions "
            "WHERE (instr(device,?)>0 OR instr(session,?)>0) AND (? IS NULL OR state=?) "
            "ORDER BY received_first DESC,device,session LIMIT ?",
            (query.lower(), query.lower(), state, state, limit))
        return [dict(row) for row in rows]

    def trial_details(self, device, session):
        return [json.loads(row[0]) for row in self.db.execute(
            "SELECT data FROM trials WHERE device=? AND session=? ORDER BY module,trial_index",
            (device, session))]

    def sample_counts(self, device, session):
        return {row[0]: row[1] for row in self.db.execute(
            "SELECT sensor,count(*) FROM samples WHERE device=? AND session=? GROUP BY sensor",
            (device, session))}
