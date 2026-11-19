"""Resumo da sessão mais recente e do movimento medido pelo MPU 1."""
import math
import pyqtgraph as pg
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QFrame
from tremo_desktop.services.dashboard import STATES
from .components import label, card, MetricCard, date_text, duration, number


class OverviewPanel(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("workspace")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(28, 25, 28, 20)
        layout.setSpacing(18)
        layout.addWidget(label("Olá!", "heading"))
        layout.addWidget(label("Aqui está um resumo do estado do dispositivo e do paciente.", "muted"))
        identity = QFrame()
        identity.setObjectName("card")
        strip = QHBoxLayout(identity)
        strip.setContentsMargins(18, 14, 18, 14)
        self.identity = []
        for icon, title in (("●", "Paciente da sessão"), ("▣", "Dispositivo"), ("▦", "Última sessão")):
            strip.addWidget(self._icon(icon, "#e0f3f2", "#107b77"))
            column = QVBoxLayout()
            column.addWidget(label(title, "muted"))
            value = label("—", "identity")
            self.identity.append(value)
            column.addWidget(value)
            strip.addLayout(column, 1)
        layout.addWidget(identity)
        self.metrics = self._cards(layout, ("Avaliações realizadas", "Sessões", "Aceleração RMS · MPU 1", "Sensores na sessão"))
        for metric, color in zip(self.metrics, ("#008f88", "#7969d9", "#239963", "#3980d5")):
            metric.setStyleSheet(f"QFrame#card {{ border-top: 3px solid {color}; }}")
        panels = QHBoxLayout()
        frame, session = card()
        title = QHBoxLayout()
        title.addWidget(label("Última sessão", "section"), 1)
        self.state = label("", "badge")
        title.addWidget(self.state)
        session.addLayout(title)
        self.timing = label("", "muted")
        session.addWidget(self.timing)
        session.addStretch()
        scores = QHBoxLayout()
        self.scores = {}
        for module, icon, color in (("mot", "◎", "#377fee"), ("rti", "ϟ", "#7969d9"),
                                    ("prm", "▧", "#239963"), ("pal", "⊞", "#e99326")):
            column = QVBoxLayout()
            column.addWidget(self._icon(icon, color, "white"), 0, Qt.AlignmentFlag.AlignCenter)
            for widget in (label(module.upper(), "eyebrow"), label("—", "scoreSmall")):
                widget.setAlignment(Qt.AlignmentFlag.AlignCenter)
                column.addWidget(widget)
            self.scores[module] = widget
            scores.addLayout(column, 1)
        session.addLayout(scores)
        session.addStretch()
        self.history = QPushButton("Ver histórico completo →")
        session.addWidget(self.history)
        panels.addWidget(frame, 1)
        frame, movement = card()
        movement.addWidget(label("Monitoramento de tremores", "section"))
        self.plot = pg.PlotWidget(background="white")
        self.plot.setLabel("bottom", "Tempo", units="s")
        self.plot.setLabel("left", "Aceleração", units="g")
        self.plot.showGrid(x=True, y=True, alpha=.15)
        self.curve = self.plot.plot(pen=pg.mkPen("#0d938e", width=2), connect="finite")
        movement.addWidget(self.plot)
        panels.addWidget(frame, 1)
        layout.addLayout(panels, 1)
        self.note = label("", "overviewNote")
        layout.addWidget(self.note)
        self.set_data(None)

    @staticmethod
    def _icon(symbol, background, foreground):
        widget = label(symbol, wrap=False)
        widget.setAlignment(Qt.AlignmentFlag.AlignCenter)
        widget.setFixedSize(34, 34)
        widget.setStyleSheet(f"border-radius:17px; background:{background}; color:{foreground}; font-size:19px;")
        return widget

    @staticmethod
    def _cards(layout, titles):
        row = QHBoxLayout()
        cards = [MetricCard(title) for title in titles]
        for widget in cards:
            row.addWidget(widget, 1)
        layout.addLayout(row)
        return cards

    def set_data(self, data):
        report = data["report"] if data else {}
        modules = report.get("modules", {})
        values = (report.get("patient_id") or (data or {}).get("patient") or "—",
                  report.get("device") or (data or {}).get("device") or "—",
                  date_text(report["first_received_utc"], short=True) if report else "—")
        for widget, value in zip(self.identity, values):
            widget.setText(value)
        sessions, runs = data["counts"] if data else (0, 0)
        self.metrics[0].set_value(str(runs))
        self.metrics[1].set_value(str(sessions))
        rows = data["samples"] if data else []
        rows = [r for r in rows if r[0] >= rows[-1][0] - 60_000_000]
        valid = [r for r in rows if r[1]]
        axes = [sum(r[i] for r in valid) / len(valid) for i in (2, 3, 4)] if valid else []
        amplitudes = [math.dist(r[2:], axes) / 16384 if r[1] else math.nan for r in rows]
        self.curve.setData([(r[0]-rows[-1][0])/1e6 for r in rows], amplitudes)
        rms = math.sqrt(sum(a*a for a in amplitudes if math.isfinite(a))/len(valid)) if valid else None
        self.metrics[2].set_value(number(rms, " g", 3), "Últimos 60 s · média por eixo removida")
        sensors = modules.get("mpu")
        self.metrics[3].set_value("—" if not sensors else "Com falhas" if any(sensors["errors"]) or not all(sensors["valid"]) else "Operacionais")
        self.state.setText(STATES.get(report.get("state"), "—") if report else "—")
        self.timing.setText(f"{values[2]}    ·    Duração {duration((report.get('timing') or {}).get('active_us'))}")
        for module in ("mot", "rti", "prm", "pal"):
            result = modules.get(module, {})
            metric = number(result.get("mean_valid_ms"), " ms") if module == "rti" else number(result.get("accuracy_percent"), "%", 1)
            self.scores[module].setText(metric)
        self.note.setText("Aguardando a primeira sessão." if not report else
                          " · ".join(report["quality"]["issues"]) or "Registros e resumos da sessão conferidos.")
