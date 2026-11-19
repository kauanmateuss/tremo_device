import json
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QComboBox, QTableWidgetItem
from PySide6.QtCore import Qt
from tremo_desktop.services.dashboard import MODULES, KINDS
from .components import label, card, number, date_text, table


def expected_modules(config):
    if not config:
        return set()
    kind = config["kind"]
    if kind in (1, 2, 3, 4):
        return {tuple(MODULES)[kind - 1]}
    if kind in (5, 6):
        return {m for index, m in enumerate(MODULES) if config["tests_mask"] & (1 << index)}
    return set()


class ResultsPanel(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("panel")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 15, 0, 0)
        layout.addWidget(label("Desempenho cognitivo", "section"))
        layout.addWidget(label("Tentativas de treino não entram nas métricas. Tempos representam as respostas aceitas pelo dispositivo.", "muted"))
        grid = self.grid = QGridLayout()
        self.frames = []
        self.columns = 2
        grid.setSpacing(14)
        self.cards = {}
        for index, (module, title) in enumerate(MODULES.items()):
            frame, box = card()
            box.setSpacing(4)
            box.setContentsMargins(16, 13, 16, 13)
            frame.setMinimumWidth(180)
            self.frames.append(frame)
            header = QHBoxLayout()
            header.addWidget(label(module.upper(), "section"))
            header.addStretch()
            status = label("", "muted")
            header.addWidget(status)
            box.addLayout(header)
            box.addWidget(label(title, "muted"))
            value = label("—", "score")
            description = label("", "muted")
            counts = label("")
            extra = label("", "muted")
            for widget in (value, description, counts, extra):
                box.addWidget(widget)
            box.addStretch()
            grid.addWidget(frame, index // 2, index % 2)
            self.cards[module] = (status, value, description, counts, extra)
        layout.addLayout(grid, 1)

    def resizeEvent(self, event):
        columns = 4 if self.width() >= 900 else 2
        if columns != self.columns:
            self.columns = columns
            for frame in self.frames:
                self.grid.removeWidget(frame)
            for index, frame in enumerate(self.frames):
                self.grid.addWidget(frame, index // columns, index % columns)
            for column in range(4):
                self.grid.setColumnStretch(column, 1 if column < columns else 0)
        super().resizeEvent(event)

    def set_data(self, data):
        report, trials = data["report"], data["trials"]
        expected = expected_modules(report["configuration"])
        for module, widgets in self.cards.items():
            status, value, description, counts, extra = widgets
            metric = report["modules"].get(module)
            training = sum(t["module"] == module and t["training"] for t in trials)
            if not metric:
                status.setText("Sem resumo" if module in expected else "Não previsto")
                value.setText("—")
                description.setText("Aguardando resultado" if module in expected and report["state"] in (1, 2) else
                                    "Resultado não recebido" if module in expected else "Não faz parte desta sessão")
                counts.setText("")
                extra.setText(f"{training} tentativas de treino recebidas" if training else "")
                continue
            verified = metric["metrics_verified"]
            status.setText("Interrompido" if metric["interrupted"] else "Limite atingido" if metric["limit_reached"] else "Finalizado")
            if not verified:
                value.setText("—")
                description.setText("Métricas indisponíveis: registros e resumo não conferem.")
                counts.setText("Consulte a qualidade e as tentativas nos detalhes.")
            elif module == "rti":
                value.setText(number(metric["mean_valid_ms"], " ms", 1))
                description.setText("Tempo médio das respostas válidas" if metric["responses"] else "Nenhuma resposta válida")
                counts.setText(f"{metric['responses']} respostas  ·  {metric['anticipations']} antecipações\n{metric['omissions']} omissões")
            else:
                value.setText(number(metric["accuracy_percent"], "%", 1))
                description.setText("Acurácia das tentativas de avaliação")
                counts.setText(f"{metric['correct']} acertos  ·  {metric['wrong']} erros  ·  {metric['omissions']} omissões\n"
                               f"Tempo médio dos acertos: {number(metric['mean_valid_ms'], ' ms', 1)}")
            note = f"Treino: {training} tentativas · excluídas das métricas"
            if module == "pal" and verified:
                note = f"{metric['levels_completed']} de 3 níveis · {metric['rounds_started']} rodadas\n" + note
            extra.setText(note)


class DetailsPanel(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("panel")
        self.data = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 15, 0, 0)
        frame, box = card()
        self.metadata = label("")
        self.metadata.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.issues = label("", "muted")
        box.addWidget(self.metadata)
        box.addWidget(self.issues)
        layout.addWidget(frame)
        filters = QHBoxLayout()
        filters.addWidget(label("Tentativas registradas", "section"))
        filters.addStretch()
        self.module = QComboBox()
        self.module.addItem("Todos os testes", None)
        for key in MODULES:
            self.module.addItem(key.upper(), key)
        self.phase = QComboBox()
        self.phase.addItem("Somente avaliação", False)
        self.phase.addItem("Somente treino", True)
        self.phase.addItem("Treino e avaliação", None)
        for widget in (self.module, self.phase):
            filters.addWidget(widget)
            widget.currentIndexChanged.connect(self.refresh_trials)
        layout.addLayout(filters)
        self.table = table(["Teste", "Etapa", "Tentativa", "Desfecho", "Tempo (ms)", "Contexto"])
        layout.addWidget(self.table, 1)
        self.empty = label("Nenhuma tentativa recebida para este filtro.", "muted")
        layout.addWidget(self.empty)
        layout.addWidget(label("RTI: tempo entre estímulo e resposta; antecipações e omissões não têm latência válida. Demais testes: tempo até a escolha, incluindo escolhas incorretas.", "muted"))

    def set_data(self, data):
        self.data = data
        r = data["report"]
        config = r["configuration"] or {}
        text = f"Sessão: {r['session']}    •    Placa: {r['device']}\n"
        text += f"Primeiro recebimento: {date_text(r['first_received_utc'])}\n"
        text += f"Último recebimento: {date_text(r['last_received_utc'])}\n"
        text += f"Paciente: {r['patient_id'] or 'Sem associação registrada'}"
        for run in r["runs"]:
            detail = json.loads(run["data"] or "{}")
            scores = (f" · {detail['correct']} acertos, {detail['wrong']} erros, {detail['omissions']} omissões"
                      if "correct" in detail else "")
            text += f"\nExecução {run['run_number']} · {run['module'].upper()} · {run['state']}{scores}"
        if config:
            text += f"\n{KINDS.get(config['kind'], 'Avaliação')} · MPU: {config['duration_ms'] // 1000} s / {1000 / config['sample_ms']:g} Hz · {config['protocol']}"
        self.metadata.setText(text)
        issues = r["quality"]["issues"]
        self.issues.setText("Qualidade: " + ("; ".join(issues) if issues else "registros e resumos conferem"))
        self.refresh_trials()

    def refresh_trials(self):
        if not self.data:
            return
        selected_module, selected_phase = self.module.currentData(), self.phase.currentData()
        rows = [t for t in self.data["trials"] if
                (selected_module is None or t["module"] == selected_module) and
                (selected_phase is None or t["training"] == selected_phase)]
        self.table.setRowCount(len(rows))
        self.empty.setVisible(not rows)
        counters = {}
        for index, trial in enumerate(rows):
            module, outcome = trial["module"], trial["outcome"]
            group = (trial["run"], module, trial["training"])
            counters[group] = counters.get(group, 0) + 1
            names = ("Resposta", "Antecipação", "Omissão", "Interrompida") if module == "rti" else ("Acerto", "Erro", "Omissão", "Interrompida")
            latency = trial.get("reaction_us") if module == "rti" else trial["elapsed_us"] if outcome in (0, 1) else None
            context = f"{trial['pairs']} pares · rodada {trial['round']}" if module == "pal" else "—"
            values = (f"{module.upper()} · {trial['run']}", "Treino" if trial["training"] else "Avaliação", str(counters[group]),
                      names[outcome], number(latency / 1000 if latency is not None else None, digits=1), context)
            for column, value in enumerate(values):
                self.table.setItem(index, column, QTableWidgetItem(value))
