# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2010 Brian J. Crowell <brian@fluggo.com>
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
from PyQt4.QtCore import *
from PyQt4.QtGui import *
import PyQt4.uic

(_type, _base) = PyQt4.uic.loadUiType(os.path.join(os.path.dirname(__file__), 'renderprogress.ui'))

class RenderProgressDialog(_base, _type):
    '''
    Dialog box that runs a render process on a background thread, shows the progress,
    and lets it be canceled. The box ends with accept() on completion or reject() on
    cancel.
    '''

    def __init__(self, muxer, encoders):
        '''
        Create a render progress dialog. The render starts as soon as the dialog is shown.

        muxer - The muxer that runs the rendering process. It needs a run() method
            that will run the render and return when it's done (or throw an exception
            on an error) and a cancel() method that will stop a run() call.
        encoders - A list of one or more encoders. Each encoder needs a "progress_count"
            attribute and a "progress" attribute that gives the encoding progress as an
            integer from zero to "progress_count".
        '''

        super().__init__()
        self.setupUi(self)

        self._encoders = encoders
        self._muxer = muxer

        self.progressBar.setRange(0, 1)
        self.progressBar.setValue(0)

        self.startTimer(125)

        self.buttonBox.rejected.connect(self._cancel_clicked)

        class MyThread(QThread):
            def __init__(self, func, parent, **kw):
                QThread.__init__(self, parent, **kw)
                self.func = func

            def run(self):
                self.func()

        self.thread = MyThread(self._run_muxer, self, finished=self._thread_finished)

    def timerEvent(self, event):
        progress = sum(a.progress for a in self._encoders)
        progress_count = sum(a.progress_count for a in self._encoders)

        self.progressBar.setRange(0, progress_count)
        self.progressBar.setValue(progress)

        self.label.setText('Rendering ({0}/{1})'.format(progress, progress_count))

    def showEvent(self, event):
        _base.showEvent(self, event)
        self.thread.start()

    def _run_muxer(self):
        try:
            self._muxer.run()
        except Exception as ex:
            print(ex)

    def _cancel_clicked(self):
        self._muxer.cancel()
        self.reject()

    def _thread_finished(self):
        self.accept()

