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
    from PySide6.QtCore import QPoint, QPointF, Qt
    from PySide6.QtGui import QWheelEvent
    from PySide6.QtTest import QTest
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
        self.assertEqual(window.pages.currentIndex(), 0)
        window.overview.history.click()
        self.assertEqual(window.pages.currentIndex(), 1)
        self.wait_for(lambda: not window.busy and window.session_list.count() == 1)
        self.assertEqual(window.tabs.currentIndex(), 2)
        window.search.clear()
        self.wait_for(lambda: window.session_list.count() == 3)
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

    def test_about_displays_reported_firmware_without_a_session(self):
        from tremo_desktop.storage.repository import Repository
        from contextlib import closing
        with closing(Repository(self.path)) as repo:
            repo.create_device("00000000000000af", "0.3-teste")
            repo.create_device("00000000000000bf", "")
        self.window.pages.setCurrentIndex(3)
        self.window.invalidate()
        self.wait_for(lambda: "00000000000000af · 0.3-teste" in self.window.about.firmware.text())
        self.assertIn("00000000000000bf · versão não informada", self.window.about.firmware.text())

    def test_compare_reference_across_filters(self):
        w = self.window
        w.pages.setCurrentIndex(1)
        w.tabs.setCurrentWidget(w.compare)
        reference = w.selected
        w.compare.pin.click()
        w.state_filter.setCurrentIndex(w.state_filter.findData(4))
        self.wait_for(lambda: not w.busy and w.snapshot.get("comparison"))
        self.assertEqual(w.reference, reference)
        self.assertEqual(w.tabs.currentWidget(), w.compare)
        self.assertEqual(w.compare.table.item(7, 1).text(), "Concluída")
        self.assertEqual(w.compare.table.item(7, 2).text(), "Interrompida")
        w.overview.history.click()
        self.wait_for(lambda: not w.busy and w.selected == reference)
        self.assertEqual(w.tabs.currentIndex(), 2)

    def test_zoom_drag_refresh_and_reset(self):
        window = self.window
        window.pages.setCurrentIndex(1)
        window.tabs.setCurrentIndex(1)
        self.application.processEvents()
        for plot in window.tremor.plots * 3:
            viewport = plot.viewport()
            pos = viewport.rect().center()
            vb = plot.getViewBox()
            xmin, xmax = vb.viewRange()[0]
            event = QWheelEvent(QPointF(pos), QPointF(viewport.mapToGlobal(pos)), QPoint(),
                                QPoint(0, 120), Qt.NoButton, Qt.NoModifier,
                                Qt.ScrollPhase.NoScrollPhase, False)
            self.application.sendEvent(viewport, event)
            self.application.processEvents()
            lo, hi = vb.viewRange()[0]
            self.assertLess(hi - lo, xmax - xmin)
            QTest.mousePress(viewport, Qt.LeftButton, Qt.NoModifier, pos)
            for delta in range(1, 10):
                QTest.mouseMove(viewport, pos + QPoint(delta * 4, delta))
                QTest.qWait(10)
            QTest.mouseRelease(viewport, Qt.LeftButton, Qt.NoModifier, pos + QPoint(36, 9))
            window.refresh(force=True)
            self.wait_for(lambda: not window.busy)
            window.tremor.reset_zoom()
            self.application.processEvents()
        self.assertTrue(window.isVisible())

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
