# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2012 Brian J. Crowell <brian@fluggo.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

from fluggo.editor import plugins
from PyQt5.QtCore import *
from PyQt5.QtGui import *
from PyQt5.QtWidgets import *
import bisect

def _make_key(alert):
    return id(alert)

class NotificationModel(QAbstractTableModel):
    def __init__(self, manager):
        super().__init__()
        self._manager = manager
        self._list = list(self._manager.alerts)
        self._list.sort(key=lambda n: _make_key(n))
        self._keys = [_make_key(n) for n in self._list]

        self._manager.alert_added.connect(self._item_added)
        self._manager.alert_removed.connect(self._item_removed)

    def _item_added(self, alert):
        index = bisect.bisect(self._keys, _make_key(alert))

        self.beginInsertRows(QModelIndex(), index, index)
        self._keys.insert(index, _make_key(alert))
        self._list.insert(index, alert)
        self.endInsertRows()

    def _item_removed(self, alert):
        index = bisect.bisect_left(self._keys, _make_key(alert))

        if index != len(self._keys) and self._keys[index] == _make_key(alert):
            self.beginRemoveRows(QModelIndex(), index, index)
            del self._list[index]
            del self._keys[index]
            self.endRemoveRows()

    def data(self, index, role=Qt.DisplayRole):
        alert = self._list[index.row()]

        if role == Qt.DisplayRole:
            if index.column() == 0:
                return alert.description
            elif index.column() == 1:
                return alert.source
        elif role == Qt.DecorationRole and index.column() == 0:
            if isinstance(alert.icon, QIcon):
                return alert.icon

            style = QApplication.style()

            if alert.icon == plugins.AlertIcon.Information:
                return style.standardIcon(QStyle.SP_MessageBoxInformation)
            elif alert.icon == plugins.AlertIcon.Warning:
                return style.standardIcon(QStyle.SP_MessageBoxWarning)
            elif alert.icon == plugins.AlertIcon.Error:
                return style.standardIcon(QStyle.SP_MessageBoxCritical)
            else:
                return None

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if orientation == Qt.Horizontal:
            if role == Qt.DisplayRole:
                if section == 0:
                    return 'Message'
                elif section == 1:
                    return 'Source'

    def flags(self, index):
        return Qt.ItemIsSelectable | Qt.ItemIsEnabled

    def rowCount(self, parent):
        if parent.isValid():
            return 0

        return len(self._list)

    def columnCount(self, parent):
        if parent.isValid():
            return 0

        return 2

class NotificationWidget(QDockWidget):
    def __init__(self, manager):
        super().__init__('Notifications')
        self._manager = manager
        self._model = NotificationModel(manager)

        widget = QWidget()

        self.view = QTableView(self)
        self.view.setModel(self._model)
        self.view.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.view.setSelectionMode(QAbstractItemView.SingleSelection)
        self.view.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.view.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeToContents)
        self.view.verticalHeader().hide()

        layout = QVBoxLayout(widget)
        layout.addWidget(self.view)

        self.setWidget(widget)


