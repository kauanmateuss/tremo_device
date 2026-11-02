from datetime import datetime
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QLabel, QFrame, QVBoxLayout, QTableWidget, QAbstractItemView, QHeaderView


def label(text="", name="", wrap=True):
    widget = QLabel(text)
    widget.setObjectName(name)
    widget.setWordWrap(wrap)
    widget.setTextFormat(Qt.TextFormat.PlainText)
    return widget


def card():
    frame = QFrame()
    frame.setObjectName("card")
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(20, 17, 20, 17)
    layout.setSpacing(8)
    return frame, layout


def number(value, suffix="", digits=0):
    if value is None:
        return "—"
    return f"{value:,.{digits}f}".replace(",", "_").replace(".", ",").replace("_", ".") + suffix


def date_text(value, short=False):
    try:
        date = datetime.fromisoformat(value).astimezone()
        return date.strftime("%d/%m · %H:%M" if short else "%d/%m/%Y às %H:%M:%S %Z")
    except (TypeError, ValueError):
        return "Horário não disponível"


def duration(us):
    if us is None:
        return "—"
    seconds = int(us / 1e6)
    return f"{seconds // 60:02d}:{seconds % 60:02d}"


class MetricCard(QFrame):
    def __init__(self, title):
        super().__init__()
        self.setObjectName("card")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(17, 15, 17, 15)
        layout.addWidget(label(title, "eyebrow"))
        self.value = label("—", "metric")
        self.caption = label("", "muted")
        layout.addWidget(self.value)
        layout.addWidget(self.caption)

    def set_value(self, value, caption=""):
        self.value.setText(value)
        self.caption.setText(caption)


def table(headers):
    widget = QTableWidget(0, len(headers))
    widget.setHorizontalHeaderLabels(headers)
    widget.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
    widget.verticalHeader().hide()
    widget.verticalHeader().setDefaultSectionSize(36)
    widget.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    widget.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    widget.setAlternatingRowColors(True)
    widget.setShowGrid(False)
    return widget
