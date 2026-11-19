"""Painel Detalhes da sessão."""
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QComboBox, QTableWidgetItem
from PySide6.QtCore import Qt
from tremo_desktop.services.dashboard import MODULES, KINDS
from .components import label, card, number, date_text, table


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
        patient = r.get("patient_id") or "não vinculado"
        text += f"Participante: {patient}\n"
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
            group = (module, trial["training"])
            counters[group] = counters.get(group, 0) + 1
            names = ("Resposta", "Antecipação", "Omissão", "Interrompida") if module == "rti" else ("Acerto", "Erro", "Omissão", "Interrompida")
            latency = trial.get("reaction_us") if module == "rti" else trial["elapsed_us"] if outcome in (0, 1) else None
            context = f"{trial['pairs']} pares · rodada {trial['round']}" if module == "pal" else "—"
            values = (module.upper(), "Treino" if trial["training"] else "Avaliação", str(counters[group]),
                      names[outcome], number(latency / 1000 if latency is not None else None, digits=1), context)
            for column, value in enumerate(values):
                self.table.setItem(index, column, QTableWidgetItem(value))
