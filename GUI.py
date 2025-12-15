import sys
import ctypes
import json
import os
from pathlib import Path
from PySide6.QtWidgets import (QApplication, QWidget, QPushButton, QLabel, QVBoxLayout, 
                                QHBoxLayout, QSpinBox, QDoubleSpinBox, QSlider, QCheckBox, 
                                QGroupBox, QColorDialog, QSystemTrayIcon, QMenu, QComboBox)
from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor, QAction, QIcon, QPixmap

class KeyButton(QPushButton):
    def __init__(self, parent=None):
        super().__init__("Click to set", parent)
        self.key_code = 0x05
        self.modifiers = {'ctrl': False, 'shift': False, 'alt': False}
        self.key_name = "Mouse 4"
        self.setText(self.key_name)
        self.recording = False
        self.temp_key_code = None
        self.temp_modifiers = {'ctrl': False, 'shift': False, 'alt': False}
        
    def get_key_name(self, vk_code):
        key_names = {
            0x08: "Backspace", 0x09: "Tab", 0x0D: "Enter", 0x10: "Shift", 0x11: "Ctrl",
            0x12: "Alt", 0x13: "Pause", 0x14: "Caps Lock", 0x1B: "Esc", 0x20: "Space",
            0x21: "Page Up", 0x22: "Page Down", 0x23: "End", 0x24: "Home",
            0x25: "Left", 0x26: "Up", 0x27: "Right", 0x28: "Down",
            0x2C: "Print Screen", 0x2D: "Insert", 0x2E: "Delete",
            0x5B: "Win", 0x5D: "Menu", 0x70: "F1", 0x71: "F2", 0x72: "F3", 0x73: "F4",
            0x74: "F5", 0x75: "F6", 0x76: "F7", 0x77: "F8", 0x78: "F9", 0x79: "F10",
            0x7A: "F11", 0x7B: "F12", 0x90: "Num Lock", 0x91: "Scroll Lock",
            0x200: "Wheel Up", 0x201: "Wheel Down"
        }
        return key_names.get(vk_code, chr(vk_code) if 0x30 <= vk_code <= 0x5A else f"Key {vk_code}")
    
    def get_display_name(self):
        parts = []
        if self.modifiers['ctrl']: parts.append('Ctrl')
        if self.modifiers['shift']: parts.append('Shift')
        if self.modifiers['alt']: parts.append('Alt')
        parts.append(self.key_name)
        return '+'.join(parts)
        
    def mousePressEvent(self, event):
        if not self.recording:
            self.recording = True
            self.setText("Press key combination...")
            self.grabKeyboard()
            self.grabMouse()
        else:
            btn = event.button()
            mouse_map = {
                Qt.LeftButton: (0x01, "LMB"),
                Qt.RightButton: (0x02, "RMB"),
                Qt.MiddleButton: (0x04, "MMB"),
                Qt.XButton1: (0x05, "Mouse 4"),
                Qt.XButton2: (0x06, "Mouse 5")
            }
            if btn in mouse_map:
                self.key_code, self.key_name = mouse_map[btn]
                self.modifiers = {'ctrl': False, 'shift': False, 'alt': False}
                self.setText(self.get_display_name())
                self.recording = False
                self.releaseKeyboard()
                self.releaseMouse()
                
    def wheelEvent(self, event):
        if self.recording:
            if event.angleDelta().y() > 0:
                self.key_code, self.key_name = (0x200, "Wheel Up")
            else:
                self.key_code, self.key_name = (0x201, "Wheel Down")
            
            self.modifiers['ctrl'] = bool(event.modifiers() & Qt.ControlModifier)
            self.modifiers['shift'] = bool(event.modifiers() & Qt.ShiftModifier)
            self.modifiers['alt'] = bool(event.modifiers() & Qt.AltModifier)
            
            self.setText(self.get_display_name())
            self.recording = False
            self.releaseKeyboard()
            self.releaseMouse()
        
    def keyPressEvent(self, event):
        if self.recording:
            if event.key() in [Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta]:
                return
            
            self.temp_key_code = event.nativeVirtualKey()
            self.temp_modifiers['ctrl'] = bool(event.modifiers() & Qt.ControlModifier)
            self.temp_modifiers['shift'] = bool(event.modifiers() & Qt.ShiftModifier)
            self.temp_modifiers['alt'] = bool(event.modifiers() & Qt.AltModifier)
            
    def keyReleaseEvent(self, event):
        if self.recording and self.temp_key_code is not None:
            self.key_code = self.temp_key_code
            self.key_name = self.get_key_name(self.key_code)
            self.modifiers = self.temp_modifiers.copy()
            
            self.setText(self.get_display_name())
            self.recording = False
            self.releaseKeyboard()
            self.releaseMouse()
            
            self.temp_key_code = None
            self.temp_modifiers = {'ctrl': False, 'shift': False, 'alt': False}

class ScopeZGUI(QWidget):
    def __init__(self):
        super().__init__()
        self.dll = None
        self.running = False
        self.script_dir = Path(__file__).parent
        self.config_file = self.script_dir / "config.json"
        self.load_config()
        self.initUI()
        self.setup_tray()
        self.setup_zoom_sync()
        
    def load_config(self):
        if self.config_file.exists():
            with open(self.config_file) as f:
                cfg = json.load(f)
                self.lens_size = cfg.get("lens_size", 300)
                self.zoom_factor = cfg.get("zoom_factor", 3.0)
                self.toggle_key = cfg.get("toggle_key", 0x05)
                self.toggle_modifiers = cfg.get("toggle_modifiers", {'ctrl': False, 'shift': False, 'alt': False})
                self.zoom_in_key = cfg.get("zoom_in_key", 0x200)
                self.zoom_in_modifiers = cfg.get("zoom_in_modifiers", {'ctrl': True, 'shift': False, 'alt': False})
                self.zoom_out_key = cfg.get("zoom_out_key", 0x201)
                self.zoom_out_modifiers = cfg.get("zoom_out_modifiers", {'ctrl': True, 'shift': False, 'alt': False})
                self.lens_shape = cfg.get("lens_shape", 0)
                self.fps = cfg.get("fps", 60)
        else:
            self.lens_size = 300
            self.zoom_factor = 3.0
            self.toggle_key = 0x05
            self.toggle_modifiers = {'ctrl': False, 'shift': False, 'alt': False}
            self.zoom_in_key = 0x200
            self.zoom_in_modifiers = {'ctrl': True, 'shift': False, 'alt': False}
            self.zoom_out_key = 0x201
            self.zoom_out_modifiers = {'ctrl': True, 'shift': False, 'alt': False}
            self.lens_shape = 0
            self.fps = 60
            
    def save_config(self):
        try:
            cfg = {
                "lens_size": self.lens_input.value(),
                "zoom_factor": self.zoom_input.value(),
                "toggle_key": self.toggle_btn.key_code,
                "toggle_modifiers": self.toggle_btn.modifiers,
                "zoom_in_key": self.zoom_in_btn.key_code,
                "zoom_in_modifiers": self.zoom_in_btn.modifiers,
                "zoom_out_key": self.zoom_out_btn.key_code,
                "zoom_out_modifiers": self.zoom_out_btn.modifiers,
                "lens_shape": self.shape_combo.currentIndex(),
                "fps": self.fps_values[self.fps_slider.value()]
            }
            with open(self.config_file, "w") as f:
                json.dump(cfg, f)
        except:
            pass
        
    def initUI(self):
        self.setWindowTitle('Scope Z')
        icon_paths = [
            self.script_dir / 'Scope Z ico.png',
            Path(sys.executable).parent / 'Scope Z ico.png',
            Path('./Scope Z ico.png')
        ]
        icon_path = None
        for path in icon_paths:
            if path.exists():
                icon_path = path
                break
        if icon_path:
            self.setWindowIcon(QIcon(str(icon_path)))
        self.setWindowFlags(Qt.FramelessWindowHint)
        self.setFixedSize(400, 520)
        self.setStyleSheet("""
            QWidget {
                background: #1e1e1e;
                color: #ffffff;
                font-family: Segoe UI;
                font-size: 10pt;
            }
            QLabel {
                color: #cccccc;
            }
            QSpinBox, QDoubleSpinBox {
                background: #2d2d2d;
                border: 1px solid #3d3d3d;
                border-radius: 4px;
                padding: 5px;
                color: #ffffff;
            }
            QSpinBox::up-button, QDoubleSpinBox::up-button {
                background: #0d7377;
                border: none;
                border-radius: 3px;
                width: 18px;
                margin: 2px;
            }
            QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover {
                background: #14a085;
            }
            QSpinBox::down-button, QDoubleSpinBox::down-button {
                background: #0d7377;
                border: none;
                border-radius: 3px;
                width: 18px;
                margin: 2px;
            }
            QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
                background: #14a085;
            }
            QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
                width: 10px;
                height: 10px;
            }
            QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
                width: 10px;
                height: 10px;
            }
            QPushButton {
                background: #0d7377;
                border: none;
                border-radius: 6px;
                padding: 8px;
                color: #ffffff;
                font-weight: bold;
            }
            QPushButton:checked {
                background: #14a085;
            }
            KeyButton {
                padding: 4px;
                font-size: 9pt;
            }
            QPushButton:hover {
                background: #14a085;
            }
            QPushButton:pressed {
                background: #0a5f62;
            }
            QGroupBox {
                border: 1px solid #3d3d3d;
                border-radius: 6px;
                margin-top: 10px;
                padding-top: 10px;
                font-weight: bold;
            }
            QGroupBox::title {
                color: #14a085;
            }
            QSlider::groove:horizontal {
                background: #2d2d2d;
                height: 6px;
                border-radius: 3px;
            }
            QSlider::handle:horizontal {
                background: #0d7377;
                width: 16px;
                margin: -5px 0;
                border-radius: 8px;
            }
            QSlider::handle:horizontal:hover {
                background: #14a085;
            }
            QCheckBox::indicator {
                width: 20px;
                height: 20px;
                border: 2px solid #3d3d3d;
                border-radius: 4px;
                background: #2d2d2d;
            }
            QCheckBox::indicator:checked {
                background: #0d7377;
                border-color: #0d7377;
            }
            QCheckBox::indicator:checked:hover {
                background: #14a085;
                border-color: #14a085;
            }
            QComboBox {
                background: #2d2d2d;
                border: 1px solid #3d3d3d;
                border-radius: 4px;
                padding: 5px;
                color: #ffffff;
            }
            QComboBox:hover {
                border-color: #0d7377;
            }
            QComboBox::drop-down {
                border: none;
                width: 20px;
            }
            QComboBox::down-arrow {
                image: none;
                border-left: 5px solid transparent;
                border-right: 5px solid transparent;
                border-top: 5px solid #0d7377;
                width: 0;
                height: 0;
            }
            QComboBox QAbstractItemView {
                background: #2d2d2d;
                border: 1px solid #0d7377;
                selection-background-color: #0d7377;
                color: #ffffff;
            }
        """)
        
        layout = QVBoxLayout()
        layout.setSpacing(0)
        layout.setContentsMargins(0, 0, 0, 0)
        
        title_bar = QWidget()
        title_bar.setFixedHeight(34)
        title_bar.setStyleSheet("background: #0d7377;")
        title_layout = QHBoxLayout(title_bar)
        title_layout.setContentsMargins(10, 0, 0, 0)
        title_layout.setSpacing(0)
        
        title_label = QLabel('Scope Z')
        title_label.setStyleSheet("color: white; font-weight: bold; font-size: 10pt;")
        title_layout.addWidget(title_label)
        title_layout.addStretch()
        
        min_btn = QPushButton('─')
        min_btn.setFixedSize(45, 34)
        min_btn.setStyleSheet("QPushButton { background: transparent; color: white; border: none; font-size: 16pt; } QPushButton:hover { background: #0a5f62; }")
        min_btn.clicked.connect(self.showMinimized)
        title_layout.addWidget(min_btn)
        
        close_btn = QPushButton('×')
        close_btn.setFixedSize(45, 34)
        close_btn.setStyleSheet("QPushButton { background: transparent; color: white; border: none; font-size: 20pt; } QPushButton:hover { background: #d32f2f; }")
        close_btn.clicked.connect(self.close)
        title_layout.addWidget(close_btn)
        
        layout.addWidget(title_bar)
        
        content = QWidget()
        content_layout = QVBoxLayout(content)
        content_layout.setSpacing(15)
        content_layout.setContentsMargins(20, 20, 20, 20)
        
        settings_group = QGroupBox("Settings")
        settings_layout = QVBoxLayout()
        
        lens_layout = QHBoxLayout()
        lens_layout.addWidget(QLabel('Lens Size:'))
        self.lens_input = QSpinBox()
        self.lens_input.setRange(50, 1000)
        self.lens_input.setSingleStep(50)
        self.lens_input.setValue(self.lens_size)
        self.lens_input.setButtonSymbols(QSpinBox.PlusMinus)
        self.lens_input.valueChanged.connect(self.apply_settings)
        lens_layout.addWidget(self.lens_input)
        settings_layout.addLayout(lens_layout)
        
        zoom_layout = QHBoxLayout()
        zoom_layout.addWidget(QLabel('Zoom Factor:'))
        self.zoom_input = QDoubleSpinBox()
        self.zoom_input.setRange(1.0, 10.0)
        self.zoom_input.setSingleStep(0.5)
        self.zoom_input.setValue(self.zoom_factor)
        self.zoom_input.setButtonSymbols(QSpinBox.PlusMinus)
        self.zoom_input.valueChanged.connect(self.apply_settings)
        zoom_layout.addWidget(self.zoom_input)
        settings_layout.addLayout(zoom_layout)
        
        fps_layout = QHBoxLayout()
        fps_layout.addWidget(QLabel('FPS Limit:'))
        self.fps_slider = QSlider(Qt.Horizontal)
        self.fps_values = [30, 60, 75, 120, 144, 240]
        self.fps_slider.setRange(0, len(self.fps_values) - 1)
        self.fps_slider.setValue(self.fps_values.index(self.fps) if self.fps in self.fps_values else 1)
        self.fps_slider.setTickPosition(QSlider.TicksBelow)
        self.fps_slider.setTickInterval(1)
        self.fps_slider.valueChanged.connect(self.on_fps_changed)
        fps_layout.addWidget(self.fps_slider)
        self.fps_label = QLabel(str(self.fps))
        self.fps_label.setMinimumWidth(40)
        self.fps_label.setStyleSheet("color: #14a085; font-weight: bold;")
        fps_layout.addWidget(self.fps_label)
        settings_layout.addLayout(fps_layout)
        
        shape_layout = QHBoxLayout()
        shape_layout.addWidget(QLabel('Lens Shape:'))
        self.shape_combo = QComboBox()
        self.shape_combo.addItems(['Circle', 'Rectangle'])
        self.shape_combo.setCurrentIndex(self.lens_shape)
        self.shape_combo.currentIndexChanged.connect(self.apply_settings)
        shape_layout.addWidget(self.shape_combo)
        settings_layout.addLayout(shape_layout)
        
        settings_group.setLayout(settings_layout)
        content_layout.addWidget(settings_group)
        
        hotkeys_group = QGroupBox("Hotkeys")
        hotkeys_layout = QVBoxLayout()
        
        toggle_layout = QHBoxLayout()
        toggle_layout.addWidget(QLabel('Toggle Key:'))
        self.toggle_btn = KeyButton()
        self.toggle_btn.key_code = self.toggle_key
        self.toggle_btn.key_name = self.toggle_btn.get_key_name(self.toggle_key)
        if hasattr(self, 'toggle_modifiers'):
            self.toggle_btn.modifiers = self.toggle_modifiers
        self.toggle_btn.setText(self.toggle_btn.get_display_name())
        self.toggle_btn.setFixedSize(120, 30)
        toggle_layout.addWidget(self.toggle_btn)
        hotkeys_layout.addLayout(toggle_layout)
        
        zoom_in_layout = QHBoxLayout()
        zoom_in_layout.addWidget(QLabel('Zoom In:'))
        self.zoom_in_btn = KeyButton()
        self.zoom_in_btn.key_code = self.zoom_in_key
        self.zoom_in_btn.key_name = self.zoom_in_btn.get_key_name(self.zoom_in_key)
        if hasattr(self, 'zoom_in_modifiers'):
            self.zoom_in_btn.modifiers = self.zoom_in_modifiers
        self.zoom_in_btn.setText(self.zoom_in_btn.get_display_name())
        self.zoom_in_btn.setFixedSize(120, 30)
        zoom_in_layout.addWidget(self.zoom_in_btn)
        hotkeys_layout.addLayout(zoom_in_layout)
        
        zoom_out_layout = QHBoxLayout()
        zoom_out_layout.addWidget(QLabel('Zoom Out:'))
        self.zoom_out_btn = KeyButton()
        self.zoom_out_btn.key_code = self.zoom_out_key
        self.zoom_out_btn.key_name = self.zoom_out_btn.get_key_name(self.zoom_out_key)
        if hasattr(self, 'zoom_out_modifiers'):
            self.zoom_out_btn.modifiers = self.zoom_out_modifiers
        self.zoom_out_btn.setText(self.zoom_out_btn.get_display_name())
        self.zoom_out_btn.setFixedSize(120, 30)
        zoom_out_layout.addWidget(self.zoom_out_btn)
        hotkeys_layout.addLayout(zoom_out_layout)
        
        hotkeys_group.setLayout(hotkeys_layout)
        content_layout.addWidget(hotkeys_group)
        
        self.launch_btn = QPushButton('▶ START')
        self.launch_btn.setMinimumHeight(50)
        self.launch_btn.clicked.connect(self.toggle)
        content_layout.addWidget(self.launch_btn)

        self.status = QLabel('● Ready')
        self.status.setAlignment(Qt.AlignCenter)
        self.status.setStyleSheet("color: #14a085; font-size: 11pt;")
        content_layout.addWidget(self.status)
        
        layout.addWidget(content)
        self.setLayout(layout)
        
        self.drag_pos = None
        title_bar.mousePressEvent = self.title_bar_mouse_press
        title_bar.mouseMoveEvent = self.title_bar_mouse_move
        
    def title_bar_mouse_press(self, event):
        if event.button() == Qt.LeftButton:
            self.drag_pos = event.globalPosition().toPoint() - self.frameGeometry().topLeft()
            
    def title_bar_mouse_move(self, event):
        if event.buttons() == Qt.LeftButton and self.drag_pos:
            self.move(event.globalPosition().toPoint() - self.drag_pos)
            
    def on_fps_changed(self):
        fps = self.fps_values[self.fps_slider.value()]
        self.fps_label.setText(str(fps))
        self.apply_settings()
            
    def apply_settings(self):
        if self.running and self.dll:
            self.save_config()
            self.dll.UpdateSettings(
                self.lens_input.value(),
                ctypes.c_float(self.zoom_input.value()),
                self.shape_combo.currentIndex(),
                0,
                0,
                0,
                0,
                0,
                self.fps_values[self.fps_slider.value()]
            )
        
    def toggle(self):
        if not self.running:
            try:
                self.save_config()
                self.dll = ctypes.CDLL(str(self.script_dir / 'scope_z.dll'))
                self.dll.StartMagnifier.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
                self.dll.UpdateSettings.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
                self.dll.GetCurrentZoom.argtypes = []
                self.dll.GetCurrentZoom.restype = ctypes.c_float
                self.dll.StartMagnifier(
                    self.lens_input.value(),
                    ctypes.c_float(self.zoom_input.value()),
                    self.toggle_btn.key_code,
                    self.zoom_in_btn.key_code,
                    1 if self.zoom_in_btn.modifiers['ctrl'] else 0,
                    1 if self.zoom_in_btn.modifiers['shift'] else 0,
                    1 if self.zoom_in_btn.modifiers['alt'] else 0,
                    self.zoom_out_btn.key_code,
                    1 if self.zoom_out_btn.modifiers['ctrl'] else 0,
                    1 if self.zoom_out_btn.modifiers['shift'] else 0,
                    1 if self.zoom_out_btn.modifiers['alt'] else 0,
                    self.shape_combo.currentIndex(),
                    0,
                    0,
                    0,
                    0,
                    0,
                    self.fps_values[self.fps_slider.value()]
                )
                self.running = True
                self.launch_btn.setText('■ STOP')
                self.launch_btn.setStyleSheet("background: #d32f2f;")
                self.status.setText('● Running')
                self.status.setStyleSheet("color: #4caf50; font-size: 11pt;")
                self.zoom_timer.start()
            except Exception as e:
                self.status.setText(f'✖ {str(e)}')
                self.status.setStyleSheet("color: #f44336; font-size: 9pt;")
        else:
            if self.dll:
                self.dll.StopMagnifier()
            self.running = False
            self.launch_btn.setText('▶ START')
            self.launch_btn.setStyleSheet("")
            self.status.setText('● Stopped')
            self.status.setStyleSheet("color: #ff9800; font-size: 11pt;")
            self.zoom_timer.stop()
            
    def setup_tray(self):
        icon_paths = [
            self.script_dir / 'Scope Z ico.png',
            Path(sys.executable).parent / 'Scope Z ico.png',
            Path('./Scope Z ico.png')
        ]
        icon_path = None
        for path in icon_paths:
            if path.exists():
                icon_path = path
                break
        
        if icon_path:
            self.tray = QSystemTrayIcon(QIcon(str(icon_path)), self)
        else:
            pixmap = QPixmap(16, 16)
            pixmap.fill(QColor('#0d7377'))
            self.tray = QSystemTrayIcon(QIcon(pixmap), self)
        menu = QMenu()
        show_action = QAction('Show', self)
        show_action.triggered.connect(self.show)
        quit_action = QAction('Exit', self)
        quit_action.triggered.connect(self.quit_app)
        menu.addAction(show_action)
        menu.addAction(quit_action)
        self.tray.setContextMenu(menu)
        self.tray.activated.connect(self.tray_clicked)
        self.tray.show()
        
    def tray_clicked(self, reason):
        if reason == QSystemTrayIcon.DoubleClick:
            self.show()
            
    def quit_app(self):
        if self.running and self.dll:
            self.dll.StopMagnifier()
        QApplication.quit()
        
    def setup_zoom_sync(self):
        self.zoom_timer = QTimer()
        self.zoom_timer.timeout.connect(self.sync_zoom)
        self.zoom_timer.setInterval(50)
        
    def sync_zoom(self):
        if self.running and self.dll:
            try:
                current_zoom = self.dll.GetCurrentZoom()
                if abs(current_zoom - self.zoom_input.value()) > 0.01:
                    self.zoom_input.blockSignals(True)
                    self.zoom_input.setValue(current_zoom)
                    self.zoom_input.blockSignals(False)
            except:
                pass
                
    def closeEvent(self, event):
        if hasattr(self, 'zoom_timer'):
            self.zoom_timer.stop()
        event.ignore()
        self.hide()

if __name__ == '__main__':
    app = QApplication(sys.argv)
    gui = ScopeZGUI()
    gui.show()
    sys.exit(app.exec()) 
