STYLE = """
QWidget { font-family: 'Inter', 'Noto Sans', 'Segoe UI', sans-serif; font-size: 13px; color: #213c46; }
QMainWindow, QWidget#workspace, QWidget#panel { background: #f3f6f7; }
QFrame#sidebar { background: #0e1b21; border-right: none; }
QLabel { background: transparent; }
QLabel#brand { font-size: 30px; font-weight: 750; color: #117d77; }
QLabel#eyebrow { font-size: 11px; font-weight: 650; color: #70858e; }
QLabel#heading { font-size: 25px; font-weight: 700; color: #18343f; }
QLabel#section { font-size: 17px; font-weight: 650; }
QLabel#muted { color: #647c85; }
QLabel#metric { font-size: 25px; font-weight: 650; }
QLabel#score { font-size: 32px; font-weight: 650; color: #117d77; }
QLabel#navTitle { color: #e8f2f4; font-size: 15px; font-weight: 650; }
QLabel#navSub { color: #9fb3b9; font-size: 11px; }
QFrame#navItem { background: transparent; border-radius: 10px; border: none; }
QFrame#navItem[active="true"] { background: #16323a; border-left: 4px solid #15887e; }
QFrame#navItem[active="true"] QLabel#navTitle { color: #ffffff; }
QFrame#navItem[active="true"] QLabel#navSub { color: #b9d2d8; }
QFrame#card { background: white; border: 1px solid #dce5e7; border-radius: 12px; }
QFrame#header { background: #f3f6f7; border-bottom: 1px solid #dce5e7; }
QFrame#banner { background: #e7f3ef; border: 1px solid #c8e5da; border-radius: 9px; }
QFrame#banner[tone="warning"] { background: #fff5e5; border-color: #f0dbb5; }
QFrame#banner[tone="neutral"] { background: #eaf1f8; border-color: #cfdeec; }
QLabel#demo { background: #fff0cc; color: #705419; padding: 10px; border-radius: 7px; font-weight: 650; }
QLineEdit, QComboBox { background: #ffffff; border: 1px solid #cbd9de; border-radius: 7px; padding: 8px; min-height: 20px; }
QLineEdit:focus, QComboBox:focus { border: 1px solid #15887e; }
QComboBox::drop-down { border: none; width: 23px; }
QComboBox QAbstractItemView { background: white; selection-background-color: #d9eee8; color: #213c46; }
QPushButton { background: white; border: 1px solid #cad9de; border-radius: 7px; padding: 9px 13px; font-weight: 600; }
QPushButton:hover { background: #eaf4f1; border-color: #5ea99e; }
QPushButton:focus { border: 2px solid #15887e; }
QPushButton:disabled { color: #a1afb4; }
QCheckBox { spacing: 8px; }
QListWidget { background: transparent; border: none; outline: none; }
QListWidget::item { padding: 13px 10px; margin: 3px 0; border-radius: 8px; }
QListWidget::item:selected { background: #e3f1ed; color: #095d58; border: 1px solid #bcded4; }
QListWidget::item:hover:!selected { background: #f2f6f7; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; padding: 12px 16px; color: #6b8089; border-bottom: 2px solid #dce5e7; }
QTabBar::tab:selected { color: #0b736b; border-bottom: 3px solid #15887e; font-weight: 650; }
QTabBar::tab:hover { color: #0b736b; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 9px; margin: 0; }
QScrollBar::handle:vertical { background: #c1cfd4; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QTableWidget { background: white; alternate-background-color: #f7fafb; border: 1px solid #dce5e7; border-radius: 7px; gridline-color: #edf2f3; selection-background-color: #dfefea; selection-color: #213c46; }
QHeaderView::section { background: #eef4f5; color: #5c747e; border: none; padding: 10px; font-weight: 600; }
QStatusBar { background: #ffffff; border-top: 1px solid #dce5e7; color: #647c85; }
QToolTip { background: #203d47; color: white; border: none; padding: 7px; }
"""
