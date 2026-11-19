"""Navegação da janela. Consultas ficam no worker; cada revisão evita respostas antigas."""
from datetime import datetime
from pathlib import Path
from PySide6.QtCore import Qt, QThread, QTimer, Signal, Slot, QSize
from PySide6.QtWidgets import (QMainWindow, QWidget, QFrame, QVBoxLayout, QHBoxLayout,
    QGridLayout, QListWidget, QListWidgetItem, QLineEdit, QComboBox, QPushButton,
    QCheckBox, QFileDialog, QStackedWidget, QScrollArea, QLabel)
from tremo_desktop.services.dashboard import STATES, KINDS
from .components import label, MetricCard, date_text, duration
from .worker import DashboardWorker
from .results import ResultsPanel
from .details import DetailsPanel
from .charts import TremorPanel
from .overview import OverviewPanel
from .history import HistoryPanel
from .settings import SettingsPanel
from .about import AboutPanel


class DashboardWindow(QMainWindow):
    requested = Signal(int, object)

    def __init__(self, path, demo=False):
        super().__init__()
        self.path = Path(path).resolve()
        self.demo = demo
        self.selected = None
        self.snapshot = None
        self.revision = 0
        self.busy = False
        self.closing = False
        self.setWindowTitle("Tremo · Dashboard de avaliações" + (" · Demonstração" if demo else ""))
        self.resize(1320, 900)
        self.setMinimumSize(1000, 720)
        self._build()
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
        self.debounce.timeout.connect(self.invalidate)
        self.overview.search.textChanged.connect(lambda: self.debounce.start())
        self.overview.state_filter.currentIndexChanged.connect(self.invalidate)
        self.overview.session_list.currentItemChanged.connect(self._selection_changed)
        self.overview.refresh_button.clicked.connect(self.invalidate)
        QTimer.singleShot(0, self.refresh)

    def _build(self):
        root = QWidget()
        self.setCentralWidget(root)
        horizontal = QHBoxLayout(root)
        horizontal.setContentsMargins(0, 0, 0, 0)
        horizontal.setSpacing(0)

        # Sidebar escura com navegação
        sidebar = QFrame()
        sidebar.setObjectName("sidebar")
        sidebar.setFixedWidth(260)
        side = QVBoxLayout(sidebar)
        side.setContentsMargins(20, 26, 20, 18)
        side.setSpacing(10)

        brand = QFrame()
        brand_layout = QVBoxLayout(brand)
        brand_layout.setContentsMargins(0, 0, 0, 0)
        brand_layout.setSpacing(6)
        brand_icon = QLabel("🧠")
        brand_icon.setStyleSheet("font-size: 34px; background: transparent;")
        brand_title = label("BitDogLab", "brand")
        brand_sub = label("Parkinson", "eyebrow")
        brand_layout.addWidget(brand_icon)
        brand_layout.addWidget(brand_title)
        brand_layout.addWidget(brand_sub)
        side.addWidget(brand)

        side.addSpacing(20)

        self.nav_visao_geral = self._nav_item("🏠", "Visão Geral", "Resumo do paciente, dispositivo e última avaliação", True)
        self.nav_historico = self._nav_item("📜", "Histórico", "Lista de sessões, testes e resultados anteriores", False)
        self.nav_configuracoes = self._nav_item("⚙️", "Configurações", "Gerenciamento do dispositivo, conexão e parâmetros", False)
        self.nav_sobre = self._nav_item("ℹ️", "Sobre", "Informações do projeto, software e hardware", False)
        for item in (self.nav_visao_geral, self.nav_historico, self.nav_configuracoes, self.nav_sobre):
            side.addWidget(item)

        side.addStretch()

        status = QFrame()
        status_layout = QVBoxLayout(status)
        status_layout.setContentsMargins(0, 0, 0, 0)
        status_layout.setSpacing(4)
        online = QLabel("● Online")
        online.setStyleSheet("color: #2ecc71; font-weight: 650; font-size: 12px; background: transparent;")
        version = label("v1.0.0", "muted")
        status_layout.addWidget(online)
        status_layout.addWidget(version)
        side.addWidget(status)

        horizontal.addWidget(sidebar)

        # Área principal com as abas
        workspace = QWidget()
        workspace.setObjectName("workspace")
        main = QVBoxLayout(workspace)
        main.setContentsMargins(0, 0, 0, 0)
        main.setSpacing(0)

        # Header da página atual
        self.header = QFrame()
        self.header.setObjectName("header")
        header_layout = QHBoxLayout(self.header)
        header_layout.setContentsMargins(26, 20, 26, 14)
        header_layout.setSpacing(0)
        self.header_title = label("Olá!", "heading")
        self.header_subtitle = label("Aqui está um resumo do estado do dispositivo e do paciente.", "muted")
        header_info = QVBoxLayout()
        header_info.setSpacing(4)
        header_info.addWidget(self.header_title)
        header_info.addWidget(self.header_subtitle)
        header_layout.addLayout(header_info, 1)
        self.status_chip = QLabel("● Conectado   BDL-0001")
        self.status_chip.setStyleSheet("""
            QLabel {
                background: #e7f3ef; color: #0e7c5d; border-radius: 10px;
                padding: 6px 14px; font-weight: 650; font-size: 12px;
            }
        """)
        self.status_chip.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        header_layout.addWidget(self.status_chip)
        main.addWidget(self.header)

        # Conteúdo das abas
        self.stack = QStackedWidget()
        self.overview = OverviewPanel(path=self.path)
        self.history = HistoryPanel()
        self.settings = SettingsPanel()
        self.about = AboutPanel()
        self.stack.addWidget(self.overview)
        self.stack.addWidget(self.history)
        self.stack.addWidget(self.settings)
        self.stack.addWidget(self.about)
        main.addWidget(self.stack, 1)

        # Delegações para compatibilidade com testes e código existente
        self.session_list = self.overview.session_list
        self.results = self.overview.results
        self.details = self.overview.details
        self.tremor = self.overview.tremor
        self.stack = self.overview.stack
        self.content = self.overview.content
        self.empty_page = self.overview.empty_page
        self.empty_title = self.overview.empty_title
        self.state_filter = self.overview.state_filter
        self.search = self.overview.search
        self.tabs = self.overview.tabs_area
        self.tabs.setCurrentIndex = lambda index: None
        self.tabs.currentIndex = lambda: 0
        self.banner = QFrame()
        self.banner.setProperty("tone", "neutral")
        self.quality_text = label("")
        self.quality_title = label("")

        horizontal.addWidget(workspace, 1)
        self.statusBar().showMessage("Consultando banco local…")

    def _nav_item(self, icon, title, subtitle, active=False):
        item = QFrame()
        item.setObjectName("navItem")
        item.setProperty("active", "true" if active else "false")
        layout = QVBoxLayout(item)
        layout.setContentsMargins(18, 12, 14, 12)
        layout.setSpacing(4)
        top = QHBoxLayout()
        top.setSpacing(10)
        icon_label = QLabel(icon)
        icon_label.setStyleSheet("font-size: 20px; background: transparent;")
        top.addWidget(icon_label)
        top.addStretch()
        status_dot = QLabel("●")
        status_dot.setFixedSize(8, 8)
        status_dot.setStyleSheet("color: #2ecc71; background: transparent;")
        top.addWidget(status_dot)
        layout.addLayout(top)
        title_label = label(title, "navTitle")
        title_label.setStyleSheet("font-size: 15px; font-weight: 650; background: transparent;")
        sub_label = label(subtitle, "navSub")
        sub_label.setStyleSheet("font-size: 11px; background: transparent;")
        layout.addWidget(title_label)
        layout.addWidget(sub_label)
        item.setCursor(Qt.CursorShape.PointingHandCursor)
        item.mousePressEvent = lambda event: self._on_nav_clicked(item)
        return item

    def _on_nav_clicked(self, item):
        for other, index in (
            (self.nav_visao_geral, 0),
            (self.nav_historico, 1),
            (self.nav_configuracoes, 2),
            (self.nav_sobre, 3),
        ):
            active = other is item
            other.setProperty("active", "true" if active else "false")
            other.style().unpolish(other)
            other.style().polish(other)
        self._navigate(index)

    def _navigate(self, index):
        self.stack.setCurrentIndex(index)
        titles = ("Olá!", "Histórico", "Configurações", "Sobre")
        subtitles = (
            "Aqui está um resumo do estado do dispositivo e do paciente.",
            "Lista de sessões, testes e resultados anteriores.",
            "Gerenciamento do dispositivo, conexão e parâmetros.",
            "Informações do projeto, software e hardware.",
        )
        self.header_title.setText(titles[index])
        self.header_subtitle.setText(subtitles[index])
        if index == 0:
            self.refresh()

    def invalidate(self):
        self.revision += 1
        self.overview.invalidate()

    def refresh(self, force=False):
        if self.closing or self.busy or (not force and not self.overview.auto_refresh.isChecked() and self.overview.snapshot is not None):
            return
        self.busy = True
        self.requested.emit(self.revision, dict(path=self.path, selected=self.selected,
                            query=self.overview.search.text(), state=self.overview.state_filter.currentData()))

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
        self.snapshot = data
        self.selected = data["selected"]
        self.overview._loaded(revision, data)
        self.history.set_sessions(data["sessions"])

    @Slot(int, str)
    def _failed(self, revision, message):
        self.busy = False
        if self.closing:
            return
        if revision != self.revision:
            self.refresh(force=True)
            return
        self.overview._failed(revision, message)

    def open_database(self):
        path, _ = QFileDialog.getOpenFileName(self, "Abrir banco de avaliações", str(self.path.parent),
                                             "SQLite (*.sqlite3 *.sqlite *.db);;Todos os arquivos (*)")
        if path:
            self.path = Path(path).resolve()
            self.selected = None
            self.snapshot = None
            self.overview.search.clear()
            self.overview.state_filter.setCurrentIndex(0)
            self.invalidate()

    def closeEvent(self, event):
        if self.thread.isRunning():
            self.closing = True
            self.timer.stop()
            self.debounce.stop()
            self.thread.quit()
            self.setEnabled(False)
            event.ignore()
        else:
            event.accept()

    @Slot()
    def _thread_finished(self):
        if self.closing:
            self.close()
