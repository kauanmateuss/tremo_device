"""Consultas da interface. Sem Qt: cada atualização usa sua própria conexão de leitura."""
import json
from pathlib import Path
from tremo_desktop.storage.repository import Repository

STATES = {None: "Estado desconhecido", 0: "Não iniciada", 1: "Em andamento",
          2: "Pausada", 3: "Concluída", 4: "Interrompida", 5: "Erro"}
KINDS = {0: "Monitoramento de tremores", 1: "Localização · MOT", 2: "Reação · RTI",
         3: "Reconhecimento · PRM", 4: "Associação · PAL", 5: "Bateria cognitiva",
         6: "Avaliação completa", 7: "Sessão de avaliações"}
MODULES = {"mot": "Localização de estímulos", "rti": "Tempo de reação",
           "prm": "Reconhecimento de padrões", "pal": "Associação de padrões"}


def load_dashboard(path, selected=None, query="", state=None):
    result = dict(sessions=[], selected=None, report=None, trials=[], samples={},
                  sample_counts={}, missing=not Path(path).is_file())
    if result["missing"]:
        return result
    repository = Repository(path, read_only=True)
    try:
        # Todas as consultas enxergam o mesmo instante, mesmo com coleta em paralelo.
        repository.db.execute("BEGIN")
        recent = repository.dashboard_sessions(limit=1)
        if recent:
            key = (recent[0]["device"], recent[0]["session"])
            report = repository.overview(*key)
            counts = repository.db.execute("""SELECT count(*), (SELECT count(*) FROM test_runs t
                JOIN sessions s USING(device,session) WHERE device=? AND s.patient_id IS ?)
                FROM sessions WHERE device=? AND patient_id IS ?""",
                (key[0], report["patient_id"], key[0], report["patient_id"])).fetchone()
            samples = repository.db.execute("""SELECT t_us,valid,ax,ay,az FROM samples
                WHERE device=? AND session=? AND sensor=1 ORDER BY t_us DESC LIMIT 3000""", key).fetchall()
            result["latest"] = dict(report=report, counts=tuple(counts), samples=samples[::-1])
        else:
            device = repository.db.execute("""SELECT d.device_id,p.patient_id FROM devices d
                LEFT JOIN patient_devices p USING(device_id) ORDER BY d.updated_at DESC LIMIT 1""").fetchone()
            if device:
                result["latest"] = dict(report={}, device=device[0], patient=device[1], counts=(0, 0), samples=[])
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
