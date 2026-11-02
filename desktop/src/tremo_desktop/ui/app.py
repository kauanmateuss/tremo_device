import sys
from tempfile import TemporaryDirectory
from pathlib import Path
from PySide6.QtWidgets import QApplication
from .style import STYLE
from .window import DashboardWindow


def run_dashboard(path, demo=False):
    application = QApplication.instance() or QApplication([sys.argv[0]])
    application.setApplicationName("Tremo")
    application.setStyle("Fusion")
    application.setStyleSheet(STYLE)
    directory = TemporaryDirectory(prefix="tremo-demo-") if demo else None
    try:
        if directory:
            from tremo_desktop.services.demo import populate_demo
            path = Path(directory.name) / "demo.sqlite3"
            populate_demo(path)
        window = DashboardWindow(path, demo=demo)
        window.show()
        return application.exec()
    finally:
        if directory:
            directory.cleanup()
