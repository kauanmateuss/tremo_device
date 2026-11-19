"""Aba Sobre: informações do projeto, software e hardware."""
from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel, QFrame
from PySide6.QtCore import Qt
from .components import label, card


class AboutPanel(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("panel")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(26, 20, 26, 16)
        layout.setSpacing(16)

        brand = QLabel("🧠")
        brand.setStyleSheet("font-size: 40px; background: transparent;")
        layout.addWidget(brand)
        layout.addWidget(label("BitDogLab Parkinson — Dashboard", "heading"))
        layout.addWidget(label("Aplicativo desktop para consulta de avaliações de monitoramento de Parkinson.", "muted"))

        info_frame, info_box = card()
        info_box.setSpacing(8)
        info_box.addWidget(label("Projeto", "section"))
        for linha in (
            "Dispositivo: BitDogLab (Raspberry Pi Pico W)",
            "Firmware: telemetria MQTT com sessões e testes cognitivos (MOT, RTI, PRM, PAL)",
            "Armazenamento: SQLite via coletor desktop (WAL, foreign_keys)",
            "Interface: PySide6 + PyQtGraph",
        ):
            info_box.addWidget(label(linha, "muted"))
        layout.addWidget(info_frame)

        tech_frame, tech_box = card()
        tech_box.setSpacing(8)
        tech_box.addWidget(label("Versões", "section"))
        for linha in (
            "Dashboard: v1.0.0",
            "Protocolo tremo: adaptado-v1",
            "Python: 3.11+",
            "PySide6: 6.x",
        ):
            tech_box.addWidget(label(linha, "muted"))
        layout.addWidget(tech_frame)

        layout.addStretch()

        nota = QLabel("Protocolo experimental adaptado · resultados para análise profissional, sem escores clínicos normativos.")
        nota.setWordWrap(True)
        nota.setStyleSheet("color: #647c85; font-size: 11px; background: transparent;")
        layout.addWidget(nota)
