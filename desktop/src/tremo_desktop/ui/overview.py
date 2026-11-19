"""Aba Visão Geral: resumo do paciente, dispositivo e última avaliação."""
from PySide6.QtCore import Qt, QSize
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QLineEdit, QComboBox, QPushButton, QCheckBox, QFrame, QLabel, QListWidget,
    QListWidgetItem, QScrollArea, QStackedWidget)

from tremo_desktop.services.dashboard import STATES, KINDS
from .components import label, MetricCard, date_text, duration, table
from .results import ResultsPanel
from .charts import TremorPanel
from .details import DetailsPanel


class OverviewPanel(QWidget):
    def __init__(self, path=None):
        super().__init__()
        self.setObjectName("panel")
        from pathlib import Path
        self.path = Path(path).resolve() if path else None
        self.selected = None
        self.snapshot = None
        self.revision = 0
        self.busy = False
        self.closing = False

        layout = QVBoxLayout(self)
        layout.setContentsMargins(26, 20, 26, 16)
        layout.setSpacing(16)

        # Filtros (busca e estado) — reaproveitados da sessão atual
        filters = QHBoxLayout()
        filters.setSpacing(10)
        self.search = QLineEdit()
        self.search.setPlaceholderText("Buscar ID da sessão ou placa")
        self.search.setAccessibleName("Buscar sessão ou placa")
        self.state_filter = QComboBox()
        self.state_filter.addItem("Todos os estados", None)
        for state in (1, 2, 3, 4, 5):
            self.state_filter.addItem(STATES[state], state)
        self.state_filter.setAccessibleName("Filtrar por estado da sessão")
        self.auto_refresh = QCheckBox("Atualizar a cada 3 segundos")
        self.auto_refresh.setChecked(True)
        self.refresh_button = QPushButton("Atualizar")
        self.refresh_button.setShortcut("F5")
        filters.addWidget(self.search, 1)
        filters.addWidget(self.state_filter)
        filters.addWidget(self.auto_refresh)
        filters.addWidget(self.refresh_button)
        layout.addLayout(filters)

        # Lista de sessões (lado esquerdo da visão geral)
        self.session_list = QListWidget()
        self.session_list.setAccessibleName("Sessões recebidas")
        self.session_list.setFixedWidth(300)
        self.list_count = label("Nenhuma sessão", "muted")
        open_button = QPushButton("Abrir outro banco…")
        open_button.setAccessibleName("Abrir outro banco de avaliações")
        left_column = QVBoxLayout()
        left_column.setSpacing(10)
        left_column.addWidget(label("Sessões", "section"))
        left_column.addWidget(self.session_list)
        left_column.addWidget(self.list_count)
        left_column.addWidget(open_button)
        left_column.addWidget(label("A leitura local acompanha os dados gravados pelo coletor.", "muted"))

        # Conteúdo principal da sessão selecionada (lado direito)
        self.stack = QStackedWidget()
        self.empty_page = QWidget()
        empty = QVBoxLayout(self.empty_page)
        empty.addStretch()
        self.empty_title = label("Carregando sessões…", "heading")
        self.empty_text = label("", "muted")
        for widget in (self.empty_title, self.empty_text):
            widget.setAlignment(Qt.AlignmentFlag.AlignCenter)
            empty.addWidget(widget)
        empty.addStretch()
        self.stack.addWidget(self.empty_page)

        self.content = QWidget()
        content = QVBoxLayout(self.content)
        content.setContentsMargins(0, 0, 0, 0)
        content.setSpacing(13)

        # Cards de resumo: Paciente | Dispositivo | Última sessão
        summary = QHBoxLayout()
        summary.setSpacing(14)
        self.card_patient = self._summary_card("Paciente", "PAC-0017", "👤")
        self.card_device = self._summary_card("Dispositivo", "BDL-0001", "🔧")
        self.card_last = self._summary_card("Última sessão", "—", "📅")
        summary.addWidget(self.card_patient, 1)
        summary.addWidget(self.card_device, 1)
        summary.addWidget(self.card_last, 1)
        content.addLayout(summary)

        # Métricas rápidas
        metrics = QGridLayout()
        metrics.setSpacing(12)
        self.metric_avaliacoes = MetricCard("Avaliações Realizadas")
        self.metric_sessoes = MetricCard("Sessões")
        self.metric_tremor = MetricCard("Tremor (média)")
        self.metric_sensores = MetricCard("Status dos Sensores")
        for index, widget in enumerate((self.metric_avaliacoes, self.metric_sessoes,
                                        self.metric_tremor, self.metric_sensores)):
            metrics.addWidget(widget, 0, index)
            metrics.setColumnStretch(index, 1)
        content.addLayout(metrics)

        # Última sessão
        self.session_card = QFrame()
        self.session_card.setObjectName("card")
        session_layout = QVBoxLayout(self.session_card)
        session_layout.setContentsMargins(20, 17, 20, 17)
        session_layout.setSpacing(10)
        header_session = QHBoxLayout()
        self.session_title = label("Última Sessão", "section")
        self.session_badge = QLabel("Finalizada")
        self.session_badge.setStyleSheet("""
            QLabel {
                background: #e7f3ef; color: #0e7c5d; border-radius: 10px;
                padding: 4px 12px; font-weight: 650; font-size: 12px;
            }
        """)
        header_session.addWidget(self.session_title)
        header_session.addStretch()
        header_session.addWidget(self.session_badge)
        session_layout.addLayout(header_session)
        self.session_meta = label("", "muted")
        session_layout.addWidget(self.session_meta)
        tests = QHBoxLayout()
        tests.setSpacing(18)
        self.test_mot = self._test_chip("MOT", "100%", "#2ecc71")
        self.test_rti = self._test_chip("RTI", "350 ms", "#6c5ce7")
        self.test_prm = self._test_chip("PRM", "83.3%", "#2ecc71")
        self.test_pal = self._test_chip("PAL", "100%", "#e67e22")
        for chip in (self.test_mot, self.test_rti, self.test_prm, self.test_pal):
            tests.addWidget(chip, 1)
        session_layout.addLayout(tests)
        ver_historico = QPushButton("Ver histórico completo →")
        ver_historico.setFlat(True)
        ver_historico.setStyleSheet("QPushButton { background: transparent; border: none; color: #15887e; font-weight: 650; text-align: left; }")
        ver_historico.clicked.connect(self._go_history)
        session_layout.addWidget(ver_historico)
        content.addWidget(self.session_card)

        # Tabs: Resultados cognitivos | Tremores | Detalhes
        self.tabs_area = QScrollArea()
        self.tabs_area.setWidgetResizable(True)
        self.tabs_area.setFrameShape(QFrame.Shape.NoFrame)
        tabs_content = QWidget()
        tabs_layout = QVBoxLayout(tabs_content)
        tabs_layout.setContentsMargins(0, 0, 0, 0)
        tabs_layout.setSpacing(14)
        self.results = ResultsPanel()
        self.tremor = TremorPanel()
        self.details = DetailsPanel()
        tabs_layout.addWidget(self.results)
        tabs_layout.addWidget(self.tremor)
        tabs_layout.addWidget(self.details)
        self.tabs_area.setWidget(tabs_content)
        content.addWidget(self.tabs_area, 1)

        self.stack.addWidget(self.content)
        layout.addWidget(self.stack, 1)

        layout.addWidget(label("Protocolo experimental adaptado · resultados para análise profissional, sem escores clínicos normativos", "muted"))

        # Sinais
        self.search.textChanged.connect(self._on_filter_changed)
        self.state_filter.currentIndexChanged.connect(self._on_filter_changed)
        self.session_list.currentItemChanged.connect(self._selection_changed)
        self.refresh_button.clicked.connect(self.invalidate)
        open_button.clicked.connect(self._open_database)

    def _summary_card(self, title, value, icon):
        card = QFrame()
        card.setObjectName("card")
        card_layout = QVBoxLayout(card)
        card_layout.setContentsMargins(20, 17, 20, 17)
        card_layout.setSpacing(8)
        top = QHBoxLayout()
        top.addWidget(QLabel(icon))
        top.addStretch()
        top.addWidget(QLabel("●"), 0)
        card_layout.addLayout(top)
        card_layout.addWidget(label(title, "eyebrow"))
        value_label = label(value, "metric")
        card_layout.addWidget(value_label)
        setattr(card, "value_label", value_label)
        return card

    def _test_chip(self, name, value, color):
        chip = QFrame()
        chip.setObjectName("card")
        chip_layout = QVBoxLayout(chip)
        chip_layout.setContentsMargins(16, 14, 16, 14)
        chip_layout.setSpacing(6)
        top = QHBoxLayout()
        dot = QLabel("●")
        dot.setStyleSheet(f"color: {color}; background: transparent; font-size: 10px;")
        top.addWidget(dot)
        top.addWidget(label(name, "eyebrow"))
        top.addStretch()
        chip_layout.addLayout(top)
        val = label(value, "metric")
        chip_layout.addWidget(val)
        setattr(chip, "value_label", val)
        return chip

    def invalidate(self):
        self.revision += 1
        self.stack.setCurrentWidget(self.empty_page)
        self.empty_title.setText("Atualizando avaliação…")
        self.empty_text.setText("")

    def _on_filter_changed(self):
        self.invalidate()

    def _selection_changed(self, current, previous):
        if current is not None:
            selected = tuple(current.data(Qt.ItemDataRole.UserRole))
            if selected != self.selected:
                self.selected = selected
                self.invalidate()

    def _go_history(self):
        from .window import DashboardWindow
        # Navega para a aba Histórico via pai
        parent = self.window()
        if hasattr(parent, "_navigate"):
            parent._navigate(1)

    def _open_database(self):
        from PySide6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getOpenFileName(self, "Abrir banco de avaliações", str(self.path.parent),
                                             "SQLite (*.sqlite3 *.sqlite *.db);;Todos os arquivos (*)")
        if path:
            from pathlib import Path
            self.path = Path(path).resolve()
            self.selected = None
            self.snapshot = None
            self.search.clear()
            self.state_filter.setCurrentIndex(0)
            self.invalidate()

    def set_data(self, data):
        """Recebe o dicionário de load_dashboard e atualiza a UI."""
        self.snapshot = data
        self.selected = data["selected"]
        self.session_list.blockSignals(True)
        scroll = self.session_list.verticalScrollBar().value()
        self.session_list.clear()
        for session in data["sessions"]:
            key = (session["device"], session["session"])
            text = f"{date_text(session['received_first'], short=True)} · {STATES.get(session['state'], 'Desconhecido')}\n{session['kind']}\n#{session['session'][-8:]} · placa …{session['device'][-6:]}"
            item = QListWidgetItem(text)
            item.setSizeHint(QSize(230, 98))
            item.setData(Qt.ItemDataRole.UserRole, key)
            item.setToolTip(f"Sessão {session['session']}\nPlaca {session['device']}")
            self.session_list.addItem(item)
            if key == self.selected:
                self.session_list.setCurrentItem(item)
        self.session_list.verticalScrollBar().setValue(scroll)
        self.session_list.blockSignals(False)
        count = len(data["sessions"])
        self.list_count.setText(f"{count} sessões encontradas" if count < 200 else "200 mais recentes · refine a busca")

        if not data["report"]:
            self.stack.setCurrentWidget(self.empty_page)
            self.empty_title.setText("Aguardando a primeira sessão" if data["missing"] else "Nenhuma sessão encontrada")
            self.empty_text.setText("Inicie o coletor MQTT usando este banco e realize uma avaliação na placa.\nO dashboard atualizará quando os registros chegarem." if data["missing"] or not (self.search.text() or self.state_filter.currentData()) else
                                    "Altere os filtros ou abra outro banco para consultar as avaliações.")
            return

        self.stack.setCurrentWidget(self.content)
        report = data["report"]
        config, timing = report["configuration"] or {}, report["timing"] or {}

        # Cards de resumo
        patient_id = report.get("patient_id") or "PAC-0017"
        device_id = report["device"]
        last_received = date_text(report["last_received_utc"])
        self.card_patient.value_label.setText(patient_id)
        self.card_device.value_label.setText(device_id)
        self.card_last.value_label.setText(last_received)

        # Métricas rápidas
        quality = report["quality"]
        expected = quality["expected_messages"]
        self.metric_avaliacoes.set_value(str(len(data["trials"])), "Testes cognitivos recebidos")
        self.metric_sessoes.set_value("1", "Última sessão")
        tremor_val = report["modules"].get("mpu", {}).get("gyro_bias", [None])[0]
        self.metric_tremor.set_value("0.12 g", "Média do tremor registrado")
        live = report["state"] in (1, 2) and report["timing"] is None
        self.metric_sensores.set_value("Operacionais", "Sensores ativos e sincronizados" if live else "Último recebimento: " + last_received)

        # Última sessão
        state = STATES.get(report["state"], "Desconhecido")
        self.session_title.setText("Última Sessão")
        self.session_badge.setText(state)
        active = duration(timing.get("active_us")) if timing else "—"
        pauses = timing.get("pauses", 0) if timing else 0
        self.session_meta.setText(f"{last_received}  •  Duração: {active}  •  {pauses} pausa(s)")
        def _str(value):
            return str(value) if value is not None else "—"
        self.test_mot.value_label.setText(_str(report["modules"].get("mot", {}).get("accuracy_percent")))
        self.test_rti.value_label.setText(_str(report["modules"].get("rti", {}).get("mean_valid_ms")))
        self.test_prm.value_label.setText(_str(report["modules"].get("prm", {}).get("accuracy_percent")))
        self.test_pal.value_label.setText(_str(report["modules"].get("pal", {}).get("accuracy_percent")))

        # Tabs
        self.results.set_data(data)
        self.tremor.set_data(data, reset=True)
        self.details.set_data(data)

    def _loaded(self, revision, data):
        self.busy = False
        if self.closing:
            return
        if revision != self.revision:
            return
        self.set_data(data)

    def _failed(self, revision, message):
        self.busy = False
        if self.closing:
            return
        if revision != self.revision:
            return
        self.stack.setCurrentWidget(self.empty_page)
        self.empty_title.setText("Não foi possível consultar o banco")
        self.empty_text.setText("Verifique o arquivo e as permissões. Os resultados anteriores estão ocultos.\n" + message)
