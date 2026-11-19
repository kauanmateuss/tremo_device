"""Aba Histórico: lista de sessões, testes e resultados anteriores."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
    QLineEdit, QComboBox, QTableWidget, QTableWidgetItem, QPushButton, QFrame)
from tremo_desktop.services.dashboard import STATES
from .components import label, table, date_text


class HistoryPanel(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("panel")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(26, 20, 26, 16)
        layout.setSpacing(12)

        # Filtros
        filters = QHBoxLayout()
        filters.setSpacing(10)
        self.search = QLineEdit()
        self.search.setPlaceholderText("Buscar ID da sessão ou placa")
        self.state_filter = QComboBox()
        self.state_filter.addItem("Todos os estados", None)
        for state in (1, 2, 3, 4, 5):
            self.state_filter.addItem(STATES[state], state)
        self.refresh_button = QPushButton("Atualizar")
        filters.addWidget(self.search, 1)
        filters.addWidget(self.state_filter)
        filters.addWidget(self.refresh_button)
        layout.addLayout(filters)

        # Tabela de histórico
        self.table = table(["Data", "Sessão", "Placa", "Tipo", "Estado", "Duração"])
        layout.addWidget(self.table, 1)

        self.empty = label("Nenhuma sessão encontrada.", "muted")
        layout.addWidget(self.empty)

        self.refresh_button.clicked.connect(self._on_refresh)
        self.search.textChanged.connect(self._on_filter_changed)
        self.state_filter.currentIndexChanged.connect(self._on_filter_changed)
        self._rows = []

    def set_sessions(self, sessions):
        self._rows = sessions
        self._apply_filters()

    def _apply_filters(self):
        query = self.search.text().strip().lower()
        state = self.state_filter.currentData()
        visible = []
        for row in self._rows:
            match_query = (query in row["device"].lower() or query in row["session"].lower()) if query else True
            match_state = (state is None or row["state"] == state)
            if match_query and match_state:
                visible.append(row)
        self.table.setRowCount(len(visible))
        self.empty.setVisible(not visible)
        for index, row in enumerate(visible):
            values = (
                date_text(row["received_first"]),
                row["session"],
                row["device"],
                row.get("kind", "—"),
                STATES.get(row["state"], "Desconhecido"),
                "—",
            )
            for column, value in enumerate(values):
                self.table.setItem(index, column, QTableWidgetItem(value))

    def _on_refresh(self):
        self._apply_filters()

    def _on_filter_changed(self):
        self._apply_filters()
