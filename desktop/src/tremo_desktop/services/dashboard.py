"""Consultas da interface. Sem Qt: cada atualização usa sua própria conexão de leitura."""
import json
import math
from pathlib import Path
from tremo_desktop.storage.repository import Repository

STATES = {None: "Estado desconhecido", 0: "Não iniciada", 1: "Em andamento",
          2: "Pausada", 3: "Concluída", 4: "Interrompida", 5: "Erro"}
KINDS = {0: "Monitoramento de tremores", 1: "Localização · MOT", 2: "Reação · RTI",
         3: "Reconhecimento · PRM", 4: "Associação · PAL", 5: "Bateria cognitiva",
         6: "Avaliação completa", 7: "Sessão de avaliações"}
MODULES = {"mot": "Localização de estímulos", "rti": "Tempo de reação",
           "prm": "Reconhecimento de padrões", "pal": "Associação de padrões"}


def latest_overview(repository, device):
    db = repository.db
    devices = [row[0] for row in db.execute("SELECT device_id FROM devices ORDER BY updated_at DESC,device_id")]
    if not devices:
        return None
    device = device if device in devices else devices[0]
    association = db.execute("""SELECT d.state,p.patient_id FROM devices d
        LEFT JOIN patient_devices p USING(device_id) WHERE d.device_id=?""", (device,)).fetchone()
    state, patient = association
    result = dict(device=device, devices=devices, patient=patient, device_state=state, report={},
                  counts=(0, 0), curves={}, rms={}, sensor_status={}, last_received=None, monitoring="Sem amostras")
    scope = (device, patient)
    sessions = db.execute("""SELECT count(*), (SELECT count(*) FROM test_runs t JOIN sessions s
        USING(device,session) WHERE device=? AND s.patient_id IS ? AND t.finished_at IS NOT NULL)
        FROM sessions WHERE device=? AND patient_id IS ?""", (*scope, *scope)).fetchone()
    result["counts"] = tuple(sessions)
    recent = db.execute("""SELECT session FROM sessions WHERE device=? AND patient_id IS ?
        ORDER BY received_first DESC,session DESC LIMIT 1""", scope).fetchone()
    if not recent:
        return result
    key = (device, recent[0])
    report = result["report"] = repository.overview(*key)
    latest = db.execute("""SELECT t_us FROM samples WHERE device=? AND session=?
        ORDER BY t_us DESC LIMIT 1""", key).fetchone()
    if not latest:
        return result
    end_us = latest[0]
    sample_ms = (report["configuration"] or {}).get("sample_ms", 40)
    for sensor in (1, 2):
        rows = repository.chart_samples(*key, sensor, after_us=max(0, end_us-60_000_000), limit=3001)
        times, axes = plot_series(rows, "acceleration_g", end_us, sample_ms)
        if rows:
            result["sensor_status"][sensor] = "OK" if rows[-1]["valid"] else "falha"
        valid = [r["acceleration_g"] for r in rows if r["valid"]]
        if valid:
            mean = [sum(axis)/len(valid) for axis in zip(*valid)]
            amplitudes = [math.dist(point, mean) for point in zip(*axes)]
            result["curves"][sensor] = (times, amplitudes)
            result["rms"][sensor] = math.sqrt(sum(math.dist(point, mean)**2 for point in valid)/len(valid))
    packet = db.execute("""SELECT received_at,seq FROM events WHERE device=? AND session=?
        AND type='samples' ORDER BY t_us DESC,seq DESC LIMIT 1""", key).fetchone()
    if packet:
        result["last_received"] = packet[0]
        finished = db.execute("""SELECT 1 FROM results WHERE device=? AND session=?
            AND module='mpu' AND seq>?""", (*key, packet[1])).fetchone()
        if report["timing"] or finished:
            result["monitoring"] = "Coleta encerrada"
        elif report["state"] == 2:
            result["monitoring"] = "Sessão pausada"
        else:
            result["monitoring"] = "Coleta sem encerramento recebido"
    return result


def load_dashboard(path, selected=None, query="", state=None, overview_device=None):
    result = dict(sessions=[], selected=None, report=None, trials=[], samples={},
                  sample_counts={}, missing=not Path(path).is_file())
    if result["missing"]:
        return result
    repository = Repository(path, read_only=True)
    try:
        # Todas as consultas enxergam o mesmo instante, mesmo com coleta em paralelo.
        repository.db.execute("BEGIN")
        result["latest"] = latest_overview(repository, overview_device)
        report = (result["latest"] or {}).get("report", {})
        key = (report.get("device"), report.get("session"))
        sessions = repository.dashboard_sessions(query.strip(), state)
        for item in sessions:
            config = json.loads(item.pop("start_json") or "null")
            item["kind"] = KINDS.get(config["kind"], "Avaliação") if config else "Início não recebido"
        result["sessions"] = sessions
        keys = [(s["device"], s["session"]) for s in sessions]
        if not keys:
            return result
        selected = selected if selected in keys else keys[0]
        result.update(selected=selected, report=report if selected == key else repository.overview(*selected),
                      trials=repository.trial_details(*selected),
                      sample_counts=repository.sample_counts(*selected),
                      samples={sensor: repository.chart_samples(*selected, sensor, limit=10000)
                               for sensor in (1, 2)})
        return result
    finally:
        repository.close()


def plot_series(rows, field, origin_us, sample_ms):
    """NaN interrompe curvas em falhas, amostras perdidas e pausas; não interpola."""
    times, axes = [], [[], [], []]
    previous = None
    nan = float("nan")
    for row in rows:
        if previous and (row["sequence"] != previous["sequence"] + 2 or
                         row["t_us"] - previous["t_us"] > sample_ms * 1500):
            times.append((row["t_us"] - origin_us) / 1e6)
            for axis in axes:
                axis.append(nan)
        times.append((row["t_us"] - origin_us) / 1e6)
        for index, axis in enumerate(axes):
            value = row[field][index] if row["valid"] else None
            axis.append(nan if value is None else value)
        previous = row
    return times, axes
