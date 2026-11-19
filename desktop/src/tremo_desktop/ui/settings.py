"""Aba Configurações: gerenciamento do dispositivo, conexão e parâmetros.

A associação de paciente ao dispositivo será implementada na próxima task.
"""
from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel, QFrame, QHBoxLayout, QPushButton
from PySide6.QtCore import Qt
from .components import label, card


class SettingsPanel(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("panel")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(26, 20, 26, 16)
        layout.setSpacing(16)

        # Dispositivo
        device_frame, device_box = card()
        device_box.setSpacing(10)
        device_box.addWidget(label("Dispositivo", "section"))
        info = QHBoxLayout()
        info.setSpacing(12)
        id_box = QFrame()
        id_box.setObjectName("card")
        id_layout = QVBoxLayout(id_box)
        id_layout.setContentsMargins(16, 13, 16, 13)
        id_layout.addWidget(label("ID do dispositivo", "eyebrow"))
        self.device_id = label("BDL-0001", "metric")
        id_layout.addWidget(self.device_id)
        info.addWidget(id_box, 1)
        fw_box = QFrame()
        fw_box.setObjectName("card")
        fw_layout = QVBoxLayout(fw_box)
        fw_layout.setContentsMargins(16, 13, 16, 13)
        fw_layout.addWidget(label("Versão do firmware", "eyebrow"))
        self.firmware = label("1.2.3", "metric")
        fw_layout.addWidget(self.firmware)
        info.addWidget(fw_box, 1)
        state_box = QFrame()
        state_box.setObjectName("card")
        state_layout = QVBoxLayout(state_box)
        state_layout.setContentsMargins(16, 13, 16, 13)
        state_layout.addWidget(label("Estado", "eyebrow"))
        self.device_state = label("Conectado", "metric")
        state_layout.addWidget(self.device_state)
        info.addWidget(state_box, 1)
        device_box.addLayout(info)
        layout.addWidget(device_frame)

        # Associação paciente-dispositivo (próxima task)
        assoc_frame, assoc_box = card()
        assoc_box.setSpacing(10)
        assoc_box.addWidget(label("Associação paciente ↔ dispositivo", "section"))
        assoc_note = label(
            "Vincule um paciente cadastrado a este BitDogLab para que as avaliações "
            "fiquem associadas ao prontuário correto. A associação será gerenciada nesta aba."
        )
        assoc_note.setWordWrap(True)
        assoc_note.setStyleSheet("color: #647c85; font-size: 12px;")
        assoc_box.addWidget(assoc_note)
        self.btn_assoc = QPushButton("Gerenciar associações")
        self.btn_assoc.setEnabled(False)
        self.btn_assoc.setToolTip("Em implementação — próxima task")
        assoc_box.addWidget(self.btn_assoc)
        layout.addWidget(assoc_frame)

        # Parâmetros
        params_frame, params_box = card()
        params_box.setSpacing(10)
        params_box.addWidget(label("Parâmetros de coleta", "section"))
        params_info = QHBoxLayout()
        params_info.setSpacing(12)
        for title, value in (("Taxa de amostragem", "40 Hz"), ("Duração padrão", "30 s"), ("Sensores MPU", "2")):
            box = QFrame()
            box.setObjectName("card")
            bl = QVBoxLayout(box)
            bl.setContentsMargins(16, 13, 16, 13)
            bl.addWidget(label(title, "eyebrow"))
            bl.addWidget(label(value, "metric"))
            params_info.addWidget(box, 1)
        params_box.addLayout(params_info)
        layout.addWidget(params_frame)

        layout.addStretch()
