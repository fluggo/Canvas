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

import fractions, threading, os.path
from PyQt5.QtCore import *
from PyQt5.QtGui import *
from PyQt5.QtWidgets import *
import PyQt5.uic
from fluggo.editor import plugins

(_type, _base) = PyQt5.uic.loadUiType(os.path.join(os.path.dirname(__file__), 'codeceditor.ui'))

class _CodecModel(QAbstractTableModel):
    def __init__(self):
        super().__init__()
        self._decoders = list(plugins.PluginManager.find_decoders(enabled_only=False))

    def rowCount(self, parent):
        if parent.isValid():
            return 0

        return len(self._decoders)

    def columnCount(self, parent):
        if parent.isValid():
            return 0

        return 2

    def data(self, index, role=Qt.DisplayRole):
        if index.column() == 0:
            if role == Qt.DisplayRole:
                return self._decoders[index.row()].name
            elif role == Qt.CheckStateRole:
                return Qt.Checked if plugins.PluginManager.is_decoder_enabled(self._decoders[index.row()]) else Qt.Unchecked
        elif index.column() == 1:
            if role == Qt.DisplayRole:
                return str(self._decoders[index.row()].priority)

    def flags(self, index):
        if index.column() == 0:
            return Qt.ItemIsSelectable | Qt.ItemIsEnabled | Qt.ItemIsUserCheckable
        elif index.column() == 1:
            return Qt.ItemIsSelectable | Qt.ItemIsEnabled

    def headerData(self, section, orientation, role = Qt.DisplayRole):
        if orientation == Qt.Horizontal:
            if section == 0:
                if role == Qt.DisplayRole:
                    return 'Name'
            elif section == 1:
                if role == Qt.DisplayRole:
                    return 'Priority'

    def setData(self, index, value, role):
        if index.column() == 0:
            if role == Qt.CheckStateRole:
                plugins.PluginManager.set_decoder_enabled(self._decoders[index.row()], value == Qt.Checked)
                self.dataChanged.emit(index, index)
                return True
        if index.column() == 1:
            if role == Qt.DisplayRole:
                decoder = self._decoders[index.row()]
                plugins.PluginManager.set_decoder_priority(decoder, int(value))

                # Find its new index
                new_list = list(plugins.PluginManager.find_decoders(enabled_only=False))
                new_index = new_list.index(decoder)

                if new_index == index.row():
                    self.dataChanged.emit(index, index)
                    return True

                if new_index > index.row():
                    # Moving down, we have to find where the target index is *now*
                    new_index += 1

                self.beginMoveRows(QModelIndex(), index.row(), index.row(),
                    QModelIndex(), new_index)
                self._decoders = new_list
                self.endMoveRows()

                return True

        return False

    def decoder_for_index(self, index):
        return self._decoders[index.row()]

class DecoderEditorDialog(_base, _type):
    def __init__(self):
        super().__init__()
        self.setupUi(self)

        self._model = _CodecModel()

        self.codecsTableView.setModel(self._model)
        self.codecsTableView.selectionModel().selectionChanged.connect(self._selection_changed)
        self.codecsTableView.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.codecsTableView.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeToContents)

        self.buttonBox.rejected.connect(self._close_clicked)

        self.upButton.clicked.connect(self._up_clicked)
        self.downButton.clicked.connect(self._down_clicked)

    def _close_clicked(self):
        self.accept()

    def _up_clicked(self):
        rows = self.codecsTableView.selectionModel().selectedRows(1)
        decoder = self._model.decoder_for_index(rows[0])
        self._model.setData(rows[0], decoder.priority + 1, Qt.DisplayRole)

    def _down_clicked(self):
        rows = self.codecsTableView.selectionModel().selectedRows(1)
        decoder = self._model.decoder_for_index(rows[0])
        self._model.setData(rows[0], decoder.priority - 1, Qt.DisplayRole)

    def _selection_changed(self, selected, deselected):
        self.upButton.setEnabled(self.codecsTableView.selectionModel().hasSelection())
        self.downButton.setEnabled(self.codecsTableView.selectionModel().hasSelection())

