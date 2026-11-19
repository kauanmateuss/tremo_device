"""Informações do projeto e versões observadas pela aplicação."""
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path
import tomllib

from PySide6.QtWidgets import QWidget, QVBoxLayout

from .components import card, label


def desktop_version():
    try:
        return version("tremo-desktop")
    except PackageNotFoundError:
        # Na execução direta a partir do repositório, o pacote pode não estar instalado.
        project = Path(__file__).resolve().parents[3] / "pyproject.toml"
        if project.is_file():
            with project.open("rb") as stream:
                return tomllib.load(stream)["project"]["version"]
        return "não informada"


class AboutPanel(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("panel")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(26, 20, 26, 16)
        layout.setSpacing(16)
        layout.addWidget(label("BitDogLab Parkinson", "heading"))
        layout.addWidget(label("O Tremo registra sinais de movimento e resultados de MOT, RTI, PRM e PAL "
                               "para acompanhar as avaliações de pessoas com Parkinson.", "muted"))

        project, box = card()
        box.addWidget(label("Equipe e instituição", "section"))
        box.addWidget(label("Kauan Mateus Vidal de Brito\nThiago Roberto Lima Ribeiro\nJoao Emanuel"))
        box.addWidget(label("Universidade Federal do Vale do São Francisco", "muted"))
        layout.addWidget(project)

        stack, box = card()
        box.addWidget(label("Dispositivo e tecnologias", "section"))
        box.addWidget(label("BitDogLab com Raspberry Pi Pico W e microcontrolador RP2040. "
                            "Os sensores MPU registram aceleração e velocidade angular."))
        box.addWidget(label("Firmware em C com Pico SDK e FreeRTOS. Comunicação MQTT via Mosquitto. "
                            "Aplicativo em Python com PySide6, PyQtGraph, Paho MQTT e SQLite.", "muted"))
        layout.addWidget(stack)

        versions, box = card()
        box.addWidget(label("Versões", "section"))
        box.addWidget(label(f"Aplicação desktop · {desktop_version()}"))
        box.addWidget(label("Firmware informado por dispositivo", "muted"))
        self.firmware = label("")
        self.set_firmware([])
        box.addWidget(self.firmware)
        layout.addWidget(versions)
        layout.addStretch()
        layout.addWidget(label("Protocolo experimental adaptado · resultados para análise profissional, "
                               "sem escores clínicos normativos.", "muted"))

    def set_firmware(self, devices):
        rows = [f"{device} · {firmware or 'versão não informada'}" for device, firmware in devices]
        self.firmware.setText("\n".join(rows) or "Nenhum dispositivo registrado neste banco.")
