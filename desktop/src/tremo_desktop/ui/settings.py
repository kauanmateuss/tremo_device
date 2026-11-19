"""Aba Configuracoes: gerenciamento do dispositivo, conexao e parametros.

Inclui a tela de gerenciamento de pacientes e dispositivos, permitindo
associar e desassociar um paciente a uma BitDogLab diretamente pela
aplicacao desktop, enviando comandos via MQTT e aguardando confirmacao.
"""
import sqlite3

from PySide6.QtWidgets import (QWidget, QVBoxLayout, QLabel, QFrame, QHBoxLayout,
    QPushButton, QComboBox, QLineEdit, QTableWidget, QTableWidgetItem,
    QHeaderView, QMessageBox, QScrollArea)
from PySide6.QtCore import Qt
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
        fw_layout.addWidget(label("Versao do firmware", "eyebrow"))
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

        # Associacao paciente <-> dispositivo
        assoc_frame, assoc_box = card()
        assoc_box.setSpacing(10)
        assoc_box.addWidget(label("Associacao paciente <-> dispositivo", "section"))
        assoc_note = label(
            "Vincule um paciente cadastrado a este BitDogLab para que as avaliacoes "
            "fiquem associadas ao prontuario correto. O comando e enviado por MQTT "
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
        self.btn_replace = QPushButton("Substituir associacao")
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
        table_box.addWidget(label("Associacoes ativas", "section"))
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

        # Cadastro rapido de dispositivo
        dev_frame, dev_box = card()
        dev_box.setSpacing(8)
        dev_box.addWidget(label("Cadastrar dispositivo", "section"))
        dev = QHBoxLayout()
        dev.setSpacing(8)
        self.new_device_id = QLineEdit()
        self.new_device_id.setPlaceholderText("ID do dispositivo (ex: BDL-0001)")
        self.new_device_fw = QLineEdit()
        self.new_device_fw.setPlaceholderText("Versao do firmware (ex: 1.2.3)")
        self.btn_new_device = QPushButton("Cadastrar")
        self.btn_new_device.clicked.connect(self._create_device)
        dev.addWidget(self.new_device_id, 1)
        dev.addWidget(self.new_device_fw, 1)
        dev.addWidget(self.btn_new_device)
        dev_box.addLayout(dev)
        layout.addWidget(dev_frame)

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
            self.patient_combo.blockSignals(False)

            # Dispositivos
            self.device_combo.blockSignals(True)
            self.device_combo.clear()
            self.device_combo.addItem("Selecione um dispositivo", None)
            for d in repository.devices():
                self.device_combo.addItem(
                    f"{d['device_id']}" + (f" — {d['firmware_version']}" if d['firmware_version'] else ""),
                    d['device_id'])
            self.device_combo.blockSignals(False)

            # Tabela de associacoes
            self._load_associations(repository)
        finally:
            repository.close()

    def _load_associations(self, repository):
        rows = []
        for d in repository.devices():
            did = d["device_id"]
            pats = repository.device_patients(did)
            for p in pats:
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
        has_both = bool(patient and device)
        self.btn_assoc.setEnabled(has_both)
        self.btn_replace.setEnabled(has_both)
        self.btn_disassoc.setEnabled(has_both and self._is_associated(patient, device))

    def _is_associated(self, patient_id, device_id):
        from tremo_desktop.storage.repository import Repository
        repository = Repository(self.path, read_only=True)
        try:
            pats = repository.device_patients(device_id)
            return any(p["patient_id"] == patient_id for p in pats)
        finally:
            repository.close()

    def _send_command(self, command, patient_id, device_id):
        """Envia comando via MQTT se collector disponivel, senao executa local."""
        if self.collector and self.selected_device:
            try:
                self.collector.publish_command(
                    self.selected_device, command,
                    {"patient_id": patient_id, "device_id": self.selected_device})
                return True
            except Exception as exc:
                QMessageBox.critical(self, "Erro MQTT",
                    f"Nao foi possível enviar o comando: {exc}")
                return False
        return None  # Sem collector: executar localmente

    def _associate(self):
        patient = self.selected_patient
        device = self.selected_device
        if not patient or not device:
            return
        # Tenta enviar via MQTT primeiro
        sent = self._send_command("associate_patient", patient, device)
        if sent is None:
            # Sem MQTT: execucao local (fallback)
            from tremo_desktop.storage.repository import Repository
            repository = Repository(self.path)
            try:
                repository.associate_patient_device(patient, device)
                QMessageBox.information(self, "Associacao",
                    f"Paciente {patient} associado ao dispositivo {device} (local).")
            except Exception as exc:
                QMessageBox.critical(self, "Erro", f"Nao foi possível associar: {exc}")
            finally:
                repository.close()
            self._refresh()
            self._update_actions()

    def _replace(self):
        patient = self.selected_patient
        device = self.selected_device
        if not patient or not device:
            return
        sent = self._send_command("associate_patient", patient, device)
        if sent is None:
            from tremo_desktop.storage.repository import Repository
            repository = Repository(self.path)
            try:
                for p in repository.device_patients(device):
                    repository.disassociate_patient_device(p["patient_id"], device)
                repository.associate_patient_device(patient, device)
                QMessageBox.information(self, "Substituicao",
                    f"Paciente {patient} agora associado ao dispositivo {device} (local).")
            except Exception as exc:
                QMessageBox.critical(self, "Erro", f"Nao foi possível substituir: {exc}")
            finally:
                repository.close()
            self._refresh()
            self._update_actions()

    def _disassociate(self):
        patient = self.selected_patient
        device = self.selected_device
        if not patient or not device:
            return
        reply = QMessageBox.question(self, "Desassociar",
            f"Deseja realmente desassociar o paciente {patient} do dispositivo {device}?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply != QMessageBox.StandardButton.Yes:
            return
        sent = self._send_command("disassociate_patient", patient, device)
        if sent is None:
            from tremo_desktop.storage.repository import Repository
            repository = Repository(self.path)
            try:
                repository.disassociate_patient_device(patient, device)
                QMessageBox.information(self, "Desassociacao",
                    f"Paciente {patient} desassociado do dispositivo {device} (local).")
            except Exception as exc:
                QMessageBox.critical(self, "Erro", f"Nao foi possível desassociar: {exc}")
            finally:
                repository.close()
            self._refresh()
            self._update_actions()

    def on_confirmation(self, confirmation):
        """Chamado pelo Collector quando uma confirmacao chega via MQTT."""
        from tremo_desktop.transport.protocol import (CONFIRM_ASSOCIATION,
            CONFIRM_DISASSOCIATION, ASSOC_STATUS_OK)
        device = confirmation["device"]
        confirm_type = confirmation["confirmation"]
        status = confirmation["status"]
        data = confirmation.get("data") or {}
        patient_id = data.get("patient_id")

        # Atualiza SQLite se for para este dispositivo e status ok
        if status == ASSOC_STATUS_OK:
            from tremo_desktop.storage.repository import Repository
            repository = Repository(self.path)
            try:
                if confirm_type == CONFIRM_ASSOCIATION and patient_id:
                    repository.associate_patient_device(patient_id, device)
                elif confirm_type == CONFIRM_DISASSOCIATION and patient_id:
                    repository.disassociate_patient_device(patient_id, device)
                self._refresh()
                self._update_actions()
                QMessageBox.information(self, "Confirmacao recebida",
                    f"Placa confirmou {confirm_type} para {patient_id} no dispositivo {device}.")
            except Exception as exc:
                QMessageBox.critical(self, "Erro", f"Nao foi possível aplicar confirmacao: {exc}")
            finally:
                repository.close()
        else:
            QMessageBox.warning(self, "Confirmacao",
                f"Placa respondeu {status} para {confirm_type} ({patient_id}/{device}).")

    def _create_patient(self):
        patient_id = self.new_patient_id.text().strip()
        name = self.new_patient_name.text().strip() or None
        if not patient_id:
            QMessageBox.warning(self, "Atencao", "Informe o ID do paciente.")
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

    def _create_device(self):
        device_id = self.new_device_id.text().strip()
        firmware = self.new_device_fw.text().strip() or "1.0.0"
        if not device_id:
            QMessageBox.warning(self, "Atencao", "Informe o ID do dispositivo.")
            return
        from tremo_desktop.storage.repository import Repository
        repository = Repository(self.path)
        try:
            repository.create_device(device_id, firmware)
            QMessageBox.information(self, "Cadastro", f"Dispositivo {device_id} cadastrado com sucesso.")
            self.new_device_id.clear()
            self.new_device_fw.clear()
        except Exception as exc:
            QMessageBox.critical(self, "Erro", f"Nao foi possível cadastrar o dispositivo: {exc}")
        finally:
            repository.close()
        self._refresh()
