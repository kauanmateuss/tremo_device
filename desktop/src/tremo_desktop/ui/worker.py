"""O worker cria/fecha o SQLite na sua thread. Apenas sinais chegam aos widgets."""
from PySide6.QtCore import QObject, Signal, Slot
from tremo_desktop.services.dashboard import load_dashboard


class DashboardWorker(QObject):
    loaded = Signal(int, object)
    failed = Signal(int, str)

    @Slot(int, object)
    def load(self, revision, request):
        try:
            self.loaded.emit(revision, load_dashboard(**request))
        except Exception as exc:
            self.failed.emit(revision, str(exc))
