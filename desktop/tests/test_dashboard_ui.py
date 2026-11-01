"""Exercita Qt sem tela física. Dependências gui são opcionais para o coletor."""
import importlib.util
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
HAS_QT = importlib.util.find_spec("PySide6") and importlib.util.find_spec("pyqtgraph")
if HAS_QT:
    from PySide6.QtWidgets import QApplication
    from tremo_desktop.ui.window import DashboardWindow
    from tremo_desktop.ui.style import STYLE
from tremo_desktop.services.demo import populate_demo
from tremo_desktop.services.dashboard import load_dashboard


@unittest.skipUnless(HAS_QT, "instale desktop[gui] para testar a interface")
class DashboardUITests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QApplication.instance() or QApplication([])
        cls.application.setStyle("Fusion")
        cls.application.setStyleSheet(STYLE)

    def wait_for(self, condition, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.application.processEvents()
            if condition(): return
            time.sleep(.01)  # Libera o GIL para as consultas Python no worker.
        self.fail("Timeout aguardando a interface")

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="tremo-ui-test-")
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "demo.sqlite3"
        populate_demo(self.path)
        self.window = DashboardWindow(self.path, demo=True)
        self.window.show()
        def close():
            self.window.close()
            self.wait_for(lambda: not self.window.thread.isRunning())
            self.window.deleteLater()
            self.application.processEvents()
        self.addCleanup(close)
        self.wait_for(lambda: self.window.snapshot is not None)

    def test_navigation_filters_trials_and_charts(self):
        window = self.window
        self.assertEqual(window.session_list.count(), 3)
        self.assertEqual(window.results.cards["mot"][1].text(), "80,0%")
        self.assertEqual(window.details.table.rowCount(), 35)
        window.details.phase.setCurrentIndex(1)
        self.assertEqual(window.details.table.rowCount(), 10)
        window.tabs.setCurrentIndex(1)
        self.assertEqual(len(window.tremor.curves[0][0].xData), 750)
        window.tremor.quantity.setCurrentIndex(1)
        self.assertLess(abs(window.tremor.curves[0][0].yData[0]), 2)
        window.session_list.setCurrentRow(1)
        self.wait_for(lambda: window.snapshot["report"]["state"] == 4)
        self.assertEqual(window.banner.property("tone"), "warning")
        self.assertIn("20 mensagens ausentes", window.quality_text.text())
        self.assertEqual(window.tabs.currentIndex(), 1)
        window.state_filter.setCurrentIndex(window.state_filter.findData(1))
        self.wait_for(lambda: len(window.snapshot["sessions"]) == 1)
        self.assertIn("andamento", window.quality_title.text())
        window.search.setText("not-found")
        self.wait_for(lambda: window.snapshot["report"] is None)
        self.assertIs(window.stack.currentWidget(), window.empty_page)

    def test_empty_invalid_database_and_recovery(self):
        window = self.window
        window.path = self.path.parent / "missing.sqlite3"
        window.invalidate()
        self.wait_for(lambda: window.snapshot["missing"])
        self.assertIn("primeira sessão", window.empty_title.text())
        self.assertFalse(window.path.exists())
        window.path.write_text("not a database")
        window.invalidate()
        self.wait_for(lambda: "Não foi possível" in window.empty_title.text())
        self.assertIs(window.stack.currentWidget(), window.empty_page)
        window.path = self.path
        window.invalidate()
        self.wait_for(lambda: window.stack.currentWidget() is window.content)

    def test_stale_requests_and_safe_close(self):
        window = self.window
        def delayed(**request):
            time.sleep(.15)
            return load_dashboard(**request)
        with patch("tremo_desktop.ui.worker.load_dashboard", delayed):
            window.invalidate()
            window.state_filter.setCurrentIndex(window.state_filter.findData(4))
            self.wait_for(lambda: not window.busy and len(window.snapshot["sessions"]) == 1)
            self.assertEqual(window.snapshot["report"]["state"], 4)
            window.invalidate()
            window.close()
            self.wait_for(lambda: not window.thread.isRunning())
            self.assertFalse(window.isVisible())


if __name__ == "__main__":
    unittest.main()
