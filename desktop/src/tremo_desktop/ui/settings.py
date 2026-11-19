"""Aba Configuracoes: gerenciamento do dispositivo, conexao e parametros.

Inclui a tela de gerenciamento de pacientes e dispositivos, permitindo
associar e desassociar um paciente a uma BitDogLab diretamente pela
aplicacao desktop, enviando comandos via MQTT e aguardando confirmacao.
"""
import sqlite3

from PySide6.QtWidgets import (QWidget, QVBoxLayout, QLabel, QFrame, QHBoxLayout,
    QPushButton, QComboBox, QLineEdit, QTableWidget, QTableWidgetItem,
    QHeaderView, QMessageBox, QScrollArea)
from PySide6.QtCore import QTimer
from .components import label, card


class SettingsPanel(QWidget):
    def __init__(self, path=None, collector=None):
        super().__init__()
        self.setObjectName("panel")
        from pathlib import Path
        self.path = Path(path).resolve() if path else None
        self.collector = collector  # Collector MQTT para enviar comandos
        self.selected_patient = None
        self.selected_device = None

        layout = QVBoxLayout(self)
        layout.setContentsMargins(26, 20, 26, 16)
        layout.setSpacing(16)

        # Associação entre paciente e dispositivo
        assoc_frame, assoc_box = card()
        assoc_box.setSpacing(10)
        assoc_box.addWidget(label("Associação entre paciente e dispositivo", "section"))
        assoc_note = label(
            "Vincule um paciente cadastrado a este BitDogLab para que as avaliações "
            "fiquem associadas ao prontuário correto. O comando é enviado por MQTT "
            "e a placa confirma a execucao."
        )
        assoc_note.setWordWrap(True)
        assoc_note.setStyleSheet("color: #647c85; font-size: 12px;")
        assoc_box.addWidget(assoc_note)

        # Selecao de paciente e dispositivo
        select = QHBoxLayout()
        select.setSpacing(10)
        self.patient_combo = QComboBox()
        self.patient_combo.addItem("Selecione um paciente", None)
        self.patient_combo.currentIndexChanged.connect(self._on_patient_changed)
        self.device_combo = QComboBox()
        self.device_combo.addItem("Selecione um dispositivo", None)
        self.device_combo.currentIndexChanged.connect(self._on_device_changed)
        select.addWidget(QLabel("Paciente:"), 0)
        select.addWidget(self.patient_combo, 1)
        select.addWidget(QLabel("Dispositivo:"), 0)
        select.addWidget(self.device_combo, 1)
        assoc_box.addLayout(select)

        # Acoes
        actions = QHBoxLayout()
        actions.setSpacing(8)
        self.btn_assoc = QPushButton("Associar")
        self.btn_assoc.setEnabled(False)
        self.btn_assoc.clicked.connect(self._associate)
        self.btn_disassoc = QPushButton("Desassociar")
        self.btn_disassoc.setEnabled(False)
        self.btn_disassoc.clicked.connect(self._disassociate)
        self.btn_replace = QPushButton("Substituir associação")
        self.btn_replace.setEnabled(False)
        self.btn_replace.clicked.connect(self._replace)
        actions.addWidget(self.btn_assoc)
        actions.addWidget(self.btn_disassoc)
        actions.addWidget(self.btn_replace)
        actions.addStretch()
        assoc_box.addLayout(actions)
        layout.addWidget(assoc_frame)

        # Tabela de associacoes atuais
        table_frame, table_box = card()
        table_box.setSpacing(8)
        table_box.addWidget(label("Dispositivos e associações", "section"))
        self.assoc_table = QTableWidget(0, 3)
        self.assoc_table.setHorizontalHeaderLabels(["Paciente", "Dispositivo", "Associado em"])
        self.assoc_table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.assoc_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.assoc_table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
        self.assoc_table.verticalHeader().hide()
        self.assoc_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.assoc_table.setAlternatingRowColors(True)
        self.assoc_table.setShowGrid(False)
        table_box.addWidget(self.assoc_table)
        layout.addWidget(table_frame)

        # Cadastro rapido de paciente
        cad_frame, cad_box = card()
        cad_box.setSpacing(8)
        cad_box.addWidget(label("Cadastrar paciente", "section"))
        cad = QHBoxLayout()
        cad.setSpacing(8)
        self.new_patient_id = QLineEdit()
        self.new_patient_id.setPlaceholderText("ID do paciente (ex: PAC-0017)")
        self.new_patient_name = QLineEdit()
        self.new_patient_name.setPlaceholderText("Nome completo")
        self.btn_new_patient = QPushButton("Cadastrar")
        self.btn_new_patient.clicked.connect(self._create_patient)
        cad.addWidget(self.new_patient_id, 1)
        cad.addWidget(self.new_patient_name, 1)
        cad.addWidget(self.btn_new_patient)
        cad_box.addLayout(cad)
        layout.addWidget(cad_frame)

        self.timeout = QTimer(self)
        self.timeout.setSingleShot(True)
        self.timeout.timeout.connect(self._expired)
        self.pending = None
        self.feedback = label("", "muted")
        layout.addWidget(self.feedback)
        layout.addStretch()

    def showEvent(self, event):
        super().showEvent(event)
        self._refresh()

    def _refresh(self):
        from tremo_desktop.storage.repository import Repository
        repository = Repository(self.path, read_only=True)
        try:
            # Pacientes
            self.patient_combo.blockSignals(True)
            self.patient_combo.clear()
            self.patient_combo.addItem("Selecione um paciente", None)
            for p in repository.patients():
                self.patient_combo.addItem(
                    f"{p['patient_id']}" + (f" — {p['name']}" if p['name'] else ""),
                    p['patient_id'])
            self.patient_combo.setCurrentIndex(max(0, self.patient_combo.findData(self.selected_patient)))
            self.selected_patient = self.patient_combo.currentData()
            self.patient_combo.blockSignals(False)

            # Dispositivos
            self.device_combo.blockSignals(True)
            self.device_combo.clear()
            self.device_combo.addItem("Selecione um dispositivo", None)
            self.device_states = {d["device_id"]: d["state"] for d in repository.devices()}
            for d in repository.devices():
                self.device_combo.addItem(
                    f"{d['device_id']}" + (f" — {d['firmware_version']}" if d['firmware_version'] else "") + f" · {d['state']}",
                    d['device_id'])
            self.device_combo.setCurrentIndex(max(0, self.device_combo.findData(self.selected_device)))
            self.selected_device = self.device_combo.currentData()
            self.device_combo.blockSignals(False)

            # Tabela de associacoes
            self._load_associations(repository)
            self._update_actions()
        finally:
            repository.close()

    def _load_associations(self, repository):
        self.associations = {}
        rows = []
        for d in repository.devices():
            did = d["device_id"]
            pats = repository.device_patients(did)
            if not pats:
                rows.append(("Sem associação", "", did, None))
            for p in pats:
                self.associations[did] = p["patient_id"]
                rows.append((p["patient_id"], p["name"] or "", did, p["associated_at"]))
        self.assoc_table.setRowCount(len(rows))
        for index, (pid, pname, did, associated_at) in enumerate(rows):
            self.assoc_table.setItem(index, 0, QTableWidgetItem(f"{pid}" + (f" — {pname}" if pname else "")))
            self.assoc_table.setItem(index, 1, QTableWidgetItem(did))
            self.assoc_table.setItem(index, 2, QTableWidgetItem(associated_at[:16] if associated_at else ""))
        self.assoc_table.resizeColumnsToContents()

    def _on_patient_changed(self, index):
        self.selected_patient = self.patient_combo.itemData(index)
        self._update_actions()

    def _on_device_changed(self, index):
        self.selected_device = self.device_combo.itemData(index)
        self._update_actions()

    def _update_actions(self):
        patient = self.selected_patient
        device = self.selected_device
        current = self.associations.get(device)
        enabled = self.device_states.get(device) != "offline" and not self.pending and bool(patient and device and self.collector and self.collector.client.is_connected())
        self.btn_assoc.setEnabled(enabled and not current)
        self.btn_replace.setEnabled(enabled and bool(current) and patient != current)
        self.btn_disassoc.setEnabled(enabled and patient == current)

    def _send(self, command):
        try:
            self.collector.publish_command(self.selected_device, command, {"patient_id": self.selected_patient})
            self.pending = (self.selected_device, self.selected_patient, command)
            self.timeout.start(12000)
            self._update_actions()
            self.feedback.setText("Comando enviado. Aguardando confirmação da placa.")
        except (RuntimeError, ValueError) as exc:
            self.feedback.setText(str(exc))

    def _associate(self):
        self._send("associate_patient")

    def _replace(self):
        self._associate()

    def _disassociate(self):
        self._send("disassociate_patient")

    def _expired(self):
        self.pending = None
        self.feedback.setText("A placa ainda não confirmou. Confira a conexão antes de tentar novamente.")
        self._update_actions()

    def on_confirmation(self, confirmation):
        command = {"association_confirmed": "associate_patient",
                   "disassociation_confirmed": "disassociate_patient"}.get(confirmation["confirmation"])
        if self.pending == (confirmation["device"], confirmation["data"]["patient_id"], command):
            self.pending = None
            self.timeout.stop()
            self.feedback.setText(f'{confirmation["device"]} · {confirmation["data"]["patient_id"]} · {confirmation["status"]}')

        self._refresh()

    def _create_patient(self):
        patient_id = self.new_patient_id.text().strip()
        name = self.new_patient_name.text().strip() or None
        from tremo_desktop.transport.protocol import valid_patient
        if not valid_patient(patient_id):
            QMessageBox.warning(self, "Atencao", "Use até 16 letras sem acento, números, hífen ou sublinhado.")
            return
        from tremo_desktop.storage.repository import Repository
        repository = Repository(self.path)
        try:
            repository.create_patient(patient_id, name)
            QMessageBox.information(self, "Cadastro", f"Paciente {patient_id} cadastrado com sucesso.")
            self.new_patient_id.clear()
            self.new_patient_name.clear()
        except sqlite3.IntegrityError:
            QMessageBox.warning(self, "Atencao", f"O paciente {patient_id} ja esta cadastrado.")
        except Exception as exc:
            QMessageBox.critical(self, "Erro", f"Nao foi possível cadastrar o paciente: {exc}")
        finally:
            repository.close()
        self._refresh()
