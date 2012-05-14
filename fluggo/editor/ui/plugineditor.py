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
from PyQt4 import QtCore, QtGui
import PyQt4.uic
from fluggo.editor import plugins

(_type, _base) = PyQt4.uic.loadUiType(os.path.join(os.path.dirname(__file__), 'plugineditor.ui'))

SUBTITLE = QtCore.Qt.UserRole

class _PluginModel(QtCore.QAbstractListModel):
    def __init__(self):
        QtCore.QAbstractListModel.__init__(self)
        self._plugins = plugins.PluginManager.find_plugins(enabled_only=False)

    def rowCount(self, parent):
        if parent.isValid():
            return 0

        return len(self._plugins)

    def data(self, index, role=QtCore.Qt.DisplayRole):
        if index.column() == 0:
            if role == QtCore.Qt.DisplayRole:
                return self._plugins[index.row()].name
            elif role == SUBTITLE:
                return self._plugins[index.row()].description
            elif role == QtCore.Qt.CheckStateRole:
                return QtCore.Qt.Checked if plugins.PluginManager.is_plugin_enabled(self._plugins[index.row()]) else QtCore.Qt.Unchecked

    def flags(self, index):
        if index.column() == 0:
            return QtCore.Qt.ItemIsSelectable | QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsUserCheckable

    def headerData(self, section, orientation, role = QtCore.Qt.DisplayRole):
        if orientation == QtCore.Qt.Horizontal:
            if section == 0:
                if role == QtCore.Qt.DisplayRole:
                    return 'Name'

    def setData(self, index, value, role):
        if index.column() == 0:
            if role == QtCore.Qt.CheckStateRole:
                plugins.PluginManager.set_plugin_enabled(self._plugins[index.row()], value == QtCore.Qt.Checked)
                self.dataChanged.emit(index, index)
                return True

        return False

class _PluginDelegate(QtGui.QStyledItemDelegate):
    def sizeHint(self, option, index):
        if index.column() != 0:
            return QtGui.QStyledItemDelegate.sizeHint(self, option, index)

        style = QtGui.QApplication.style()
        margin = style.pixelMetric(QtGui.QStyle.PM_FocusFrameHMargin, option, option.widget)
        metrics = QtGui.QFontMetrics(option.font)

        bold_font = QtGui.QFont(option.font)
        bold_font.setBold(True)
        bold_font.setPointSize(bold_font.pointSize() + 2)
        bold_metrics = QtGui.QFontMetrics(bold_font)

        return QtCore.QSize(10, bold_metrics.height() + metrics.lineSpacing() + margin * 2)

    def paint(self, painter, option, index):
        if index.column() != 0:
            return QtGui.QStyledItemDelegate.paint(self, painter, option, index)

        option.features |= QtGui.QStyleOptionViewItemV4.HasCheckIndicator
        option.features |= QtGui.QStyleOptionViewItemV4.HasDisplay
        option.text = 'blah'

        painter.save()
        painter.setClipRect(option.rect)

        # A good chunk of this is copied from QCommonStyle to duplicate
        # the look of normal items
        style = QtGui.QApplication.style()
        style.drawPrimitive(QtGui.QStyle.PE_PanelItemViewItem, option, painter, option.widget)

        # Draw checkbox
        button_option = QtGui.QStyleOptionViewItemV4(option)
        button_option.rect = style.subElementRect(QtGui.QStyle.SE_ItemViewItemCheckIndicator, option, option.widget)
        button_option.state = option.state & ~QtGui.QStyle.State_HasFocus

        if index.data(QtCore.Qt.CheckStateRole) == QtCore.Qt.Checked:
            button_option.state |= QtGui.QStyle.State_On
        else:
            button_option.state |= QtGui.QStyle.State_Off

        style.drawPrimitive(QtGui.QStyle.PE_IndicatorItemViewItemCheck, button_option, painter, option.widget)

        # Draw text
        margin = style.pixelMetric(QtGui.QStyle.PM_FocusFrameHMargin, option, option.widget)
        metrics = QtGui.QFontMetrics(option.font)

        bold_font = QtGui.QFont(option.font)
        bold_font.setBold(True)
        bold_font.setPointSize(bold_font.pointSize() + 2)
        bold_metrics = QtGui.QFontMetrics(bold_font)

        baserect = style.subElementRect(QtGui.QStyle.SE_ItemViewItemText, option, option.widget)
        baserect.setHeight(bold_metrics.height())

        cg = QtGui.QPalette.Normal if (option.state & QtGui.QStyle.State_Enabled) else QtGui.QPalette.Disabled

        title_text = index.data(QtCore.Qt.DisplayRole) or ''
        title_text = bold_metrics.elidedText(title_text, QtCore.Qt.ElideRight, baserect.width() - 2 * margin)

        subtitle_text = index.data(SUBTITLE) or ''
        subtitle_text = metrics.elidedText(subtitle_text, QtCore.Qt.ElideRight, baserect.width() - 2 * margin)

        painter.save()

        try:
            painter.setPen(option.palette.color(cg,
                QtGui.QPalette.HighlightedText if (option.state & QtGui.QStyle.State_Selected) else QtGui.QPalette.Text))

            painter.setFont(bold_font)
            painter.drawText(baserect.x() + margin, baserect.y() + margin + bold_metrics.ascent(),
                title_text)

            painter.setFont(option.font)
            painter.drawText(baserect.x() + margin, baserect.y() + margin + bold_metrics.ascent() + metrics.lineSpacing(),
                subtitle_text)
        finally:
            painter.restore()

        # Draw focus
        if option.state & QtGui.QStyle.State_HasFocus == QtGui.QStyle.State_HasFocus:
            focus_option = QtGui.QStyleOptionFocusRect()
            focus_option.state = option.state

            focus_option.rect = style.subElementRect(QtGui.QStyle.SE_ItemViewItemFocusRect, option, option.widget)
            focus_option.state |= QtGui.QStyle.State_Item | QtGui.QStyle.State_KeyboardFocusChange

            focus_option.background_color = option.palette.color(cg,
                QtGui.QPalette.Highlight if (option.state & QtGui.QStyle.State_Selected) else QtGui.QPalette.Window)

            style.drawPrimitive(QtGui.QStyle.PE_FrameFocusRect, focus_option, painter, option.widget)

        painter.restore()

class PluginEditorDialog(_base, _type):
    def __init__(self):
        _base.__init__(self)
        _type.__init__(self)
        self.setupUi(self)

        self._model = _PluginModel()
        self._delegate = _PluginDelegate()

        self.pluginsListView.setModel(self._model)
        self.pluginsListView.setItemDelegate(self._delegate)

        self.buttonBox.rejected.connect(self._close_clicked)

    def _close_clicked(self):
        self.accept()

