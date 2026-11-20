"""Navegação da janela. Consultas ficam no worker; cada revisão evita respostas antigas."""
import os
import sqlite3
from hashlib import sha256
from datetime import datetime
from pathlib import Path
from PySide6.QtCore import Qt, QThread, QTimer, Signal, Slot, QSize, QDate
from PySide6.QtWidgets import (QMainWindow, QWidget, QFrame, QVBoxLayout, QHBoxLayout,
    QGridLayout, QListWidget, QListWidgetItem, QLineEdit, QComboBox, QPushButton,
    QCheckBox, QDateEdit, QFileDialog, QStackedWidget, QTabWidget, QScrollArea)
from tremo_desktop.services.dashboard import STATES, KINDS
from .components import label, MetricCard, date_text, duration
from .worker import DashboardWorker
from .results import ResultsPanel, DetailsPanel
from .charts import TremorPanel
from .settings import SettingsPanel
from .about import AboutPanel
from .overview import OverviewPanel
from .comparison import ComparisonPanel
from tremo_desktop.transport.mqtt import Collector


class DashboardWindow(QMainWindow):
    requested = Signal(int, object)
    confirmation_received = Signal(object)

    def __init__(self, path, demo=False):
        super().__init__()
        self.path = Path(path).resolve()
        self.demo = demo
        self.selected = None
        self.snapshot = None
        self.reference = None
        self.session_limit = 200
        self.revision = 0
        self.busy = False
        self.closing = False
        self.collector = None
        self.setWindowTitle("Tremo · Dashboard de avaliações" + (" · Demonstração" if demo else ""))
        self.resize(1320, 900)
        self.setMinimumSize(1000, 720)
        self._build()
        # Inicializa o coletor MQTT (nao no modo demo)
        if not self.demo:
            from tremo_desktop.storage.repository import Repository
            Repository(self.path).close()
            self.confirmation_received.connect(self.settings.on_confirmation)
            self.settings.broker_changed.connect(self._save_broker)
            self._connect_broker(self.settings.host.text(), self.settings.port.value())
        self.thread = QThread(self)
        self.worker = DashboardWorker()
        self.worker.moveToThread(self.thread)
        self.requested.connect(self.worker.load)
        self.worker.loaded.connect(self._loaded)
        self.worker.failed.connect(self._failed)
        self.thread.finished.connect(self.worker.deleteLater)
        self.thread.finished.connect(self._thread_finished)
        self.thread.start()
        self.timer = QTimer(self)
        self.timer.setInterval(3000)
        self.timer.timeout.connect(self.refresh)
        self.timer.start()
        self.debounce = QTimer(self)
        self.debounce.setSingleShot(True)
        self.debounce.setInterval(250)
        self.debounce.timeout.connect(self.filter_changed)
        self.search.textChanged.connect(lambda: self.debounce.start())
        for combo in (self.state_filter, self.patient_filter, self.device_filter):
            combo.currentIndexChanged.connect(self.filter_changed)
        self.date_filter.dateChanged.connect(self.filter_changed)
        self.session_list.currentItemChanged.connect(self._selection_changed)
        self.refresh_button.clicked.connect(self.invalidate)
        QTimer.singleShot(0, self.refresh)

    def _connect_broker(self, host, port):
        if self.collector:
            self.collector.client.disconnect()
            self.collector.client.loop_stop()
        database_id = sha256(os.fsencode(self.path)).hexdigest()[:16]
        self.collector = Collector(self.path, client_id=f"tremo-gui-{database_id}",
            username=self.settings.broker_user.text() or None,
            password=self.settings.broker_password.text() or None,
            on_confirmation=self.confirmation_received.emit)
        self.settings.collector = self.collector
        self.collector.client.connect_async(host, port)
        self.collector.client.loop_start()

    def _save_broker(self, host, port):
        self._connect_broker(host, port)
        self.settings.preferences.setValue("broker/host", host)
        self.settings.preferences.setValue("broker/port", port)

    def _build(self):
        root = QWidget()
        self.setCentralWidget(root)
        horizontal = QHBoxLayout(root)
        horizontal.setContentsMargins(0, 0, 0, 0)
        horizontal.setSpacing(0)
        sidebar = QFrame()
        sidebar.setObjectName("sidebar")
        sidebar.setFixedWidth(185)
        nav = QVBoxLayout(sidebar)
        nav.setContentsMargins(14, 30, 14, 18)
        nav.addWidget(label("BitDogLab\nParkinson", "brand"))
        nav.addSpacing(22)
        self.pages = QStackedWidget()
        for index, title in enumerate(("Visão Geral", "Histórico", "Configurações", "Sobre")):
            button = QPushButton(title)
            button.setObjectName("navigation")
            button.setCheckable(True)
            button.setAutoExclusive(True)
            button.setChecked(index == 0)
            button.clicked.connect(lambda checked, i=index: self.pages.setCurrentIndex(i))
            self.pages.currentChanged.connect(lambda i, n=index, b=button: b.setChecked(i == n))
            nav.addWidget(button)
        nav.addStretch()
        self.network_text = label("Demonstração" if self.demo else "MQTT desconectado", "network")
        nav.addWidget(self.network_text)
        horizontal.addWidget(sidebar)
        horizontal.addWidget(self.pages, 1)
        self.overview = OverviewPanel()
        self.overview.history.clicked.connect(self.open_latest)
        self.overview.device_choice.currentIndexChanged.connect(self.invalidate)
        self.pages.addWidget(self.overview)
        history = QWidget()
        history_layout = QHBoxLayout(history)
        self.pages.addWidget(history)
        filters = QWidget()
        filters.setFixedWidth(225)
        side = QVBoxLayout(filters)
        side.addWidget(label("Sessões", "section"))
        self.search = QLineEdit()
        self.search.setPlaceholderText("Buscar ID da sessão ou placa")
        self.search.setAccessibleName("Buscar sessão ou placa")
        side.addWidget(self.search)
        self.state_filter = QComboBox()
        self.state_filter.addItem("Todos os estados", None)
        for state in (1, 2, 3, 4, 5):
            self.state_filter.addItem(STATES[state], state)
        self.state_filter.setAccessibleName("Filtrar por estado da sessão")
        side.addWidget(self.state_filter)
        self.patient_filter = QComboBox()
        self.device_filter = QComboBox()
        for combo, title in ((self.patient_filter, "Todos os pacientes"), (self.device_filter, "Todos os dispositivos")):
            combo.addItem(title, None)
            combo.setAccessibleName(title)
            side.addWidget(combo)
        self.date_filter = QDateEdit()
        self.date_filter.setCalendarPopup(True)
        self.date_filter.setDisplayFormat("dd/MM/yyyy")
        self.date_filter.setSpecialValueText("Todas as datas")
        self.date_filter.setDate(self.date_filter.minimumDate())
        self.date_filter.calendarWidget().setCurrentPage(QDate.currentDate().year(), QDate.currentDate().month())
        self.date_filter.setAccessibleName("Data local da sessão")
        side.addWidget(self.date_filter)
        clear = QPushButton("Limpar filtros")
        clear.clicked.connect(self.clear_filters)
        side.addWidget(clear)
        self.session_list = QListWidget()
        self.session_list.setAccessibleName("Sessões recebidas")
        side.addWidget(self.session_list, 1)
        self.list_count = label("Nenhuma sessão", "muted")
        side.addWidget(self.list_count)
        self.more = QPushButton("Carregar mais sessões")
        self.more.clicked.connect(self.load_more)
        self.more.hide()
        side.addWidget(self.more)
        self.auto_refresh = QCheckBox("Atualizar a cada 3 segundos")
        self.auto_refresh.setChecked(True)
        side.addWidget(self.auto_refresh)
        open_button = QPushButton("Abrir outro banco…")
        open_button.setEnabled(not self.demo)
        open_button.clicked.connect(self.open_database)
        side.addWidget(open_button)
        side.addWidget(label("A leitura local acompanha os dados gravados pelo coletor.", "muted"))
        history_layout.addWidget(filters)
        workspace = QWidget()
        workspace.setObjectName("workspace")
        main = QVBoxLayout(workspace)
        main.setContentsMargins(26, 23, 26, 16)
        main.setSpacing(15)
        if self.demo:
            main.addWidget(label("DEMONSTRAÇÃO · Dados simulados, separados das avaliações reais", "demo"))
        top = QHBoxLayout()
        titles = QVBoxLayout()
        titles.setSpacing(4)
        titles.addWidget(label("Histórico", "heading"))
        titles.addWidget(label("Resultados, movimento e qualidade da coleta", "muted"))
        top.addLayout(titles, 1)
        self.refresh_button = QPushButton("Atualizar")
        self.refresh_button.setShortcut("F5")
        top.addWidget(self.refresh_button)
        main.addLayout(top)
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
        self.session_title = label("", "section")
        self.session_caption = label("", "muted")
        content.addWidget(self.session_title)
        content.addWidget(self.session_caption)
        metrics = QGridLayout()
        metrics.setSpacing(12)
        self.metrics = [MetricCard(name) for name in ("ESTADO DA SESSÃO", "TEMPO ATIVO", "TENTATIVAS RECEBIDAS", "MENSAGENS RECEBIDAS")]
        for index, widget in enumerate(self.metrics):
            metrics.addWidget(widget, 0, index)
            metrics.setColumnStretch(index, 1)
        content.addLayout(metrics)
        self.banner = QFrame()
        self.banner.setObjectName("banner")
        banner_layout = QVBoxLayout(self.banner)
        banner_layout.setContentsMargins(14, 11, 14, 11)
        self.quality_title = label("")
        self.quality_text = label("", "muted")
        banner_layout.addWidget(self.quality_title)
        banner_layout.addWidget(self.quality_text)
        content.addWidget(self.banner)
        self.tabs = QTabWidget()
        self.results = ResultsPanel()
        self.tremor = TremorPanel()
        self.details = DetailsPanel()
        self.settings = SettingsPanel(self.path, collector=self.collector)
        self.about = AboutPanel()
        for widget, title in ((self.results, "Resultados cognitivos"), (self.tremor, "Tremores"), (self.details, "Detalhes da sessão")):
            scroll = QScrollArea()
            scroll.setWidgetResizable(True)
            scroll.setWidget(widget)
            self.tabs.addTab(scroll, title)
        self.compare = ComparisonPanel()
        self.compare.pin.clicked.connect(self.pin_reference)
        self.tabs.addTab(self.compare, "Comparar")
        content.addWidget(self.tabs, 1)
        self.stack.addWidget(self.content)
        main.addWidget(self.stack, 1)
        main.addWidget(label("Protocolo experimental adaptado · resultados para análise profissional, sem escores clínicos normativos", "muted"))
        history_layout.addWidget(workspace, 1)
        for panel in (self.settings, self.about):
            scroll = QScrollArea()
            scroll.setWidgetResizable(True)
            scroll.setWidget(panel)
            self.pages.addWidget(scroll)
        self.statusBar().showMessage("Consultando banco local…")

    def filter_changed(self):
        self.session_limit = 200
        self.invalidate()

    def clear_filters(self):
        self.search.clear()
        for combo in (self.patient_filter, self.device_filter, self.state_filter):
            combo.setCurrentIndex(0)
        self.date_filter.setDate(self.date_filter.minimumDate())
        self.debounce.stop()
        self.filter_changed()

    def load_more(self):
        self.session_limit += 200
        self.invalidate()

    def pin_reference(self):
        self.reference = self.selected
        self.invalidate()

    @staticmethod
    def update_filter(combo, values):
        if [combo.itemData(i) for i in range(1, combo.count())] == values:
            return
        current = combo.currentData()
        combo.blockSignals(True)
        while combo.count() > 1:
            combo.removeItem(combo.count()-1)
        for value in values:
            combo.addItem(value or "Sem paciente", value)
        combo.setCurrentIndex(max(0, combo.findData(current)))
        combo.blockSignals(False)

    def open_latest(self):
        report = self.snapshot["latest"]["report"]
        self.clear_filters()
        self.device_filter.setCurrentIndex(max(0, self.device_filter.findData(report["device"])))
        self.selected = (report["device"], report["session"])
        self.search.setText(report["session"])
        self.state_filter.setCurrentIndex(0)
        self.debounce.stop()
        self.pages.setCurrentIndex(1)
        self.tabs.setCurrentIndex(2)
        self.invalidate()

    @Slot()
    def invalidate(self):
        self.revision += 1
        self.stack.setCurrentWidget(self.empty_page)
        self.empty_title.setText("Atualizando avaliação…")
        self.empty_text.setText("")
        self.refresh(force=True)

    def refresh(self, force=False):
        if self.closing or self.busy or (not force and not self.auto_refresh.isChecked() and self.snapshot is not None):
            return
        self.busy = True
        self.requested.emit(self.revision, dict(path=self.path, selected=self.selected,
                            query=self.search.text(), state=self.state_filter.currentData(),
                            overview_device=self.overview.device_choice.currentText(),
                            patient=self.patient_filter.currentData(), device=self.device_filter.currentData(),
                            day=self.date_filter.date().toString("yyyy-MM-dd") if self.date_filter.date() != self.date_filter.minimumDate() else None,
                            limit=self.session_limit, reference=self.reference))

    def _selection_changed(self, current, previous):
        if current is not None:
            selected = tuple(current.data(Qt.ItemDataRole.UserRole))
            if selected != self.selected:
                self.selected = selected
                self.invalidate()

    @Slot(int, object)
    def _loaded(self, revision, data):
        self.busy = False
        if self.closing:
            return
        if revision != self.revision:
            self.refresh(force=True)
            return
        changed = not self.snapshot or self.snapshot["selected"] != data["selected"]
        self.about.set_firmware(data.get("firmware_versions", []))
        self.overview.set_data(data.get("latest"))
        if self.collector:
            self.network_text.setText("MQTT conectado" if self.collector.client.is_connected() else "MQTT desconectado")
            if self.settings.isVisible():
                self.settings._refresh()
        self.snapshot = data
        self.selected = data["selected"]
        self.update_filter(self.patient_filter, data["patients"])
        self.update_filter(self.device_filter, data["devices"])
        self.more.setVisible(data["has_more"])
        self.session_list.blockSignals(True)
        scroll = self.session_list.verticalScrollBar().value()
        self.session_list.clear()
        for session in data["sessions"]:
            key = (session["device"], session["session"])
            status = STATES.get(session["state"], "Desconhecido")
            date = date_text(session["received_first"], short=True)
            patient = session["patient_id"] or "Sem paciente"
            quality = " · Incompleta" if session["incomplete"] else ""
            text = f"{date}\n{status}{quality}\n{patient}\n#{key[1][-8:]} · placa …{key[0][-6:]}"
            item = QListWidgetItem(text)
            item.setSizeHint(QSize(160, 105))
            item.setData(Qt.ItemDataRole.UserRole, key)
            item.setToolTip(f"{session['kind']} · {patient}\nSessão {key[1]}\nPlaca {key[0]}")
            self.session_list.addItem(item)
            if key == self.selected:
                self.session_list.setCurrentItem(item)
        self.session_list.verticalScrollBar().setValue(scroll)
        self.session_list.blockSignals(False)
        count = len(data["sessions"])
        self.list_count.setText(f"{count} sessões exibidas")
        self.statusBar().showMessage(f"Consulta: {datetime.now():%H:%M:%S}  •  Banco: {self.path.name}")
        self.statusBar().setToolTip(str(self.path))
        if not data["report"]:
            self.stack.setCurrentWidget(self.empty_page)
            self.empty_title.setText("Aguardando a primeira sessão" if data["missing"] else "Nenhuma sessão encontrada")
            self.empty_text.setText("Altere os filtros para consultar outras avaliações." if data["devices"] else
                                    "O histórico será atualizado quando o coletor receber uma sessão.")
            return
        self.stack.setCurrentWidget(self.content)
        report = data["report"]
        config, timing = report["configuration"] or {}, report["timing"] or {}
        self.session_title.setText(KINDS.get(config.get("kind"), "Avaliação · início não recebido"))
        self.session_caption.setText(f"Sessão {report['session']}  •  Recebida em {date_text(report['first_received_utc'])}")
        self.metrics[0].set_value(STATES.get(report["state"], "Desconhecido"), "Informado pela placa")
        self.metrics[1].set_value(duration(timing.get("active_us")),
                                 f"{timing.get('pauses', 0)} pausas · {duration(timing.get('paused_us'))} em pausa" if timing else "Disponível após o encerramento")
        assessed = sum(not t["training"] for t in data["trials"])
        self.metrics[2].set_value(str(assessed), "Avaliação · sem treino")
        quality = report["quality"]
        expected = quality["expected_messages"]
        self.metrics[3].set_value(str(quality["received_messages"]), f"de {expected} esperadas" if expected is not None else "Total esperado ainda não recebido")
        live = report["state"] in (1, 2) and report["timing"] is None
        issues = quality["issues"]
        live_issues = [issue for issue in issues if issue != "encerramento ausente"]
        tone = "neutral" if live and not live_issues else "good" if quality["complete"] else "warning"
        self.banner.setProperty("tone", tone)
        self.banner.style().unpolish(self.banner)
        self.banner.style().polish(self.banner)
        self.quality_title.setText("Sessão em andamento · confira as ocorrências da coleta" if live and live_issues else
                                   "Sessão em andamento · integridade ainda não concluída" if live else
                                   "Dados conferidos · registros e resumos consistentes" if quality["complete"] else "Atenção à qualidade dos dados")
        self.quality_text.setText("Último recebimento: " + date_text(report["last_received_utc"]) if live and not live_issues else
                                 "A integridade dos registros é independente da conclusão da avaliação." if not issues else
                                 "; ".join(issues[:3]) + (". Veja todas as ocorrências nos detalhes." if len(issues) > 3 else "."))
        self.results.set_data(data)
        self.tremor.set_data(data, reset=changed)
        self.details.set_data(data)
        self.compare.set_data(report, self.reference, data["comparison"])
        if changed and self.tabs.currentIndex() < 2:
            self.tabs.setCurrentIndex(1 if config.get("kind") == 0 else 0)

    @Slot(int, str)
    def _failed(self, revision, message):
        self.busy = False
        if self.closing:
            return
        if revision != self.revision:
            self.refresh(force=True)
            return
        self.stack.setCurrentWidget(self.empty_page)
        self.overview.set_data(None)
        self.empty_title.setText("Não foi possível consultar o banco")
        self.empty_text.setText("Verifique o arquivo e as permissões. Os resultados anteriores estão ocultos.\n" + message)
        self.statusBar().showMessage("Falha na consulta · tente atualizar ou abrir outro banco")

    def open_database(self):
        path, _ = QFileDialog.getOpenFileName(self, "Abrir banco de avaliações", str(self.path.parent),
                                             "SQLite (*.sqlite3 *.sqlite *.db);;Todos os arquivos (*)")
        if path:
            from tremo_desktop.storage.repository import Repository
            from PySide6.QtWidgets import QMessageBox
            try:
                Repository(path, read_only=True).close()
            except (OSError, RuntimeError, sqlite3.Error) as exc:
                QMessageBox.warning(self, "Abrir banco", str(exc))
                return
            if self.collector:
                self.collector.client.disconnect()
                self.collector.client.loop_stop()
                self.collector = self.settings.collector = None
                self.network_text.setText("Arquivo local")
            self.settings.btn_new_patient.setEnabled(False)
            self.path = self.settings.path = Path(path).resolve()
            self.about.set_firmware([])
            self.selected = None
            self.snapshot = None
            self.reference = None
            self.clear_filters()

    def closeEvent(self, event):
        if self.collector:
            try:
                self.collector.client.disconnect()
                self.collector.client.loop_stop()
            except Exception:
                pass
        if self.thread.isRunning():
            self.closing = True
            self.timer.stop()
            self.debounce.stop()
            self.thread.quit()
            self.setEnabled(False)
            event.ignore()  # O fechamento termina após a consulta em curso, sem destruir a thread.
        else:
            event.accept()

    @Slot()
    def _thread_finished(self):
        if self.closing:
            self.close()
