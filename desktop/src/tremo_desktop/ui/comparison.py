from PySide6.QtWidgets import QWidget, QPushButton, QVBoxLayout, QTableWidgetItem

from tremo_desktop.services.dashboard import STATES
from .components import label, table, date_text, number, duration


def session_values(report):
    if not report:
        return ["—"] * 11
    modules = report["modules"]
    values = []
    for key in ("mot", "rti", "prm", "pal"):
        module = modules.get(key, {})
        if key == "rti":
            values.append(number(module.get("mean_valid_ms"), " ms", 1))
        else:
            values.append(number(module.get("accuracy_percent"), "%", 1))
    values.extend([report["patient_id"] or "Sem paciente", report["device"],
              date_text(report["first_received_utc"]), STATES.get(report["state"], "Desconhecido"),
              "Conferidos" if report["quality"]["complete"] else "Com pendências",
              duration((report["timing"] or {}).get("active_us")),
              str(sum(r["finished_at"] is not None for r in report["runs"]))])
    return values


class ComparisonPanel(QWidget):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout(self)
        self.caption = label("", "muted")
        layout.addWidget(self.caption)
        self.pin = QPushButton("Usar sessão selecionada como referência")
        layout.addWidget(self.pin)
        self.table = table(["Indicador", "Referência", "Selecionada"])
        layout.addWidget(self.table)
        labels = ("MOT · acurácia", "RTI · reação média", "PRM · acurácia", "PAL · acurácia",
                  "Paciente", "Dispositivo", "Data", "Estado", "Integridade", "Tempo ativo", "Testes finalizados")
        self.table.setRowCount(len(labels))
        for row, title in enumerate(labels):
            self.table.setItem(row, 0, QTableWidgetItem(title))
        layout.addWidget(label("Cada módulo mostra a última execução com resumo. Métricas sem dados conferidos aparecem como —.", "muted"))

    def set_data(self, selected, reference, comparison):
        key = (selected["device"], selected["session"])
        same = reference == key
        self.pin.setEnabled(not same)
        if same:
            message = "Referência definida. Selecione outra sessão na lista para comparar."
        elif comparison:
            message = f"Referência {comparison['session']}  ·  Selecionada {selected['session']}"
        else:
            message = "Escolha uma sessão de referência e depois selecione outra sessão no histórico."
        self.caption.setText(message)
        for column, report in enumerate((selected if same else comparison, selected), 1):
            for row, value in enumerate(session_values(report)):
                self.table.setItem(row, column, QTableWidgetItem(value))
