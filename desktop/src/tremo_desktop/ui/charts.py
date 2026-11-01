"""Gráficos com a mesma referência de tempo e falhas visíveis como interrupções."""
import pyqtgraph as pg
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QComboBox, QPushButton
from tremo_desktop.services.dashboard import plot_series
from .components import label, number

AXIS_COLORS = ("#15887e", "#6479d1", "#d09136")


class TremorPanel(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("panel")
        self.data = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 15, 0, 0)
        top = QHBoxLayout()
        top.addWidget(label("Movimento registrado", "section"))
        top.addStretch()
        for axis, color in zip("XYZ", AXIS_COLORS):
            legend = label(f"● {axis}", wrap=False)
            legend.setStyleSheet(f"color: {color}; font-weight: 600;")
            top.addWidget(legend)
        self.quantity = QComboBox()
        self.quantity.addItem("Velocidade angular · °/s", "angular_velocity_dps")
        self.quantity.addItem("Aceleração · g", "acceleration_g")
        self.quantity.currentIndexChanged.connect(self.change_quantity)
        top.addWidget(self.quantity)
        reset = QPushButton("Ajustar zoom")
        reset.clicked.connect(self.reset_zoom)
        top.addWidget(reset)
        layout.addLayout(top)
        self.summary = label("Sem leituras nesta sessão.", "muted")
        layout.addWidget(self.summary)
        self.plots, self.curves = [], []
        plots_layout = QHBoxLayout()
        plots_layout.setSpacing(12)
        for sensor in (1, 2):
            plot = pg.PlotWidget(background="white")
            plot.setMinimumWidth(230)
            plot.setFixedHeight(220)
            plot.setTitle(f"MPU {sensor}", color="#213c46", size="11pt")
            plot.showGrid(x=True, y=True, alpha=.12)
            plot.setLabel("bottom", "Tempo desde a primeira leitura", units="s")
            plot.setMenuEnabled(False)
            plot.getAxis("bottom").setTextPen("#647c85")
            plot.getAxis("left").setTextPen("#647c85")
            self.curves.append([plot.plot([], [], pen=pg.mkPen(color, width=1.4),
                                           name=axis, connect="finite")
                                for axis, color in zip("XYZ", AXIS_COLORS)])
            self.plots.append(plot)
            plots_layout.addWidget(plot, 1)
        layout.addLayout(plots_layout, 1)
        self.plots[1].setXLink(self.plots[0])
        layout.addWidget(label("Arraste para explorar; use a roda para ampliar. Lacunas e leituras inválidas interrompem as curvas. Aceleração inclui gravidade.", "muted"))

    def set_data(self, data, reset=False):
        self.data = data
        self.redraw()
        if reset:
            self.reset_zoom()

    def change_quantity(self):
        self.redraw()
        self.reset_zoom()

    def reset_zoom(self):
        for plot in self.plots:
            plot.enableAutoRange()

    def redraw(self):
        if not self.data:
            return
        data = self.data
        samples = data["samples"]
        report = data["report"]
        field = self.quantity.currentData()
        starts = [rows[0]["t_us"] for rows in samples.values() if rows]
        origin = min(starts) if starts else 0
        config = report["configuration"] or {}
        result = report["modules"].get("mpu", {})
        period = result.get("sample_ms", config.get("sample_ms", 40))
        counts = data["sample_counts"]
        for sensor, plot, curves in zip((1, 2), self.plots, self.curves):
            times, axes = plot_series(samples.get(sensor, []), field, origin, period)
            for curve, values in zip(curves, axes):
                curve.setData(times, values, connect="finite")
            plot.setLabel("left", "Velocidade angular" if field == "angular_velocity_dps" else "Aceleração",
                          units="°/s" if field == "angular_velocity_dps" else "g")
            plot.setTitle(f"MPU {sensor}" + (" · sem leituras" if not samples.get(sensor) else ""), color="#213c46", size="11pt")
        if not starts:
            self.summary.setText("Nenhuma leitura recebida para esta sessão.")
            return
        text = f"MPU 1: {number(counts.get(1, 0))} leituras   •   MPU 2: {number(counts.get(2, 0))} leituras"
        if result:
            coverage = result["coverage"]
            text += f"   •   Cobertura: {number(coverage[0] * 100 if coverage[0] is not None else None, '%', 1)} / {number(coverage[1] * 100 if coverage[1] is not None else None, '%', 1)}"
            text += "\n" + ("Calibração concluída" if result["calibrated"] else "Sem calibração")
            text += f"   •   Intervalos perdidos: {result['missed_slots']}   •   Descartes na aquisição: {result['buffer_drops']}   •   Falhas de leitura: {sum(result['errors'])}"
        else:
            text += "\nResumo da aquisição ainda não recebido."
        if any(counts.get(sensor, 0) > len(samples.get(sensor, [])) for sensor in (1, 2)):
            text += "\nExibição limitada às primeiras 10.000 leituras de cada sensor."
        self.summary.setText(text)
