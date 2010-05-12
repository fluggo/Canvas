
from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4.QtOpenGL import *
from fluggo.media import process, timecode, qt
import sys, fractions, array

videro = process.FFVideoSource('/home/james/Videos/Soft Boiled/Sources/softboiled01;03;21;24.avi')
pulldown = process.Pulldown23RemovalFilter(videro, 0);

red = process.SolidColorVideoSource((1.0, 0.0, 0.0, 0.25), (20, 20, 318, 277))
green = process.SolidColorVideoSource((0.0, 1.0, 0.0, 0.75), (200, 200, 518, 477))

frame = pulldown.get_frame_f32(0, (200, 100, 519, 278))

workspace = process.Workspace()
workspace_item = workspace.add(source=pulldown, x=0, width=100, z=0)
workspace.add(source=red, x=50, width=100, z=1)
workspace.add(source=green, x=75, width=100, z=2)
workspace.add(source=frame, x=125, width=100, z=0, offset=500)

#videro = process.VideoMixFilter(src_a=process.SolidColorVideoSource((1.0, 0.0, 0.0, 0.25), (1, 0, 718, 477)), src_b=process.SolidColorVideoSource((0.0, 1.0, 0.0, 0.75), (2, 1, 717, 476)), mix_b=process.LinearFrameFunc(a=1/300.0, b=0))
audio = process.FFAudioSource('/home/james/Videos/Soft Boiled/Sources/BWFF/1B_1.wav')
player = process.AlsaPlayer(48000, source=audio)
clock = player

NS_PER_SEC = fractions.Fraction(1000000000L, 1)

class TimeRuler(QWidget):
    SMALL_TICK_THRESHOLD = 2
    current_frame_changed = pyqtSignal(float, name='currentFrameChanged')

    def __init__(self, parent=None, timecode=timecode.Frames(), scale=fractions.Fraction(1), frame_rate=fractions.Fraction(30, 1)):
        QWidget.__init__(self, parent)
        self.frame_rate = fractions.Fraction(frame_rate)
        self.set_timecode(timecode)
        self.set_scale(scale)
        self.left_frame = 0
        self.current_frame = 0

    def sizeHint(self):
        return QSize(60, 30)

    def set_left_frame(self, left_frame):
        left_frame = int(left_frame)

        if left_frame != self.left_frame:
            self.left_frame = left_frame
            self.update()

    def set_current_frame(self, frame):
        frame = int(frame)

        if self.current_frame != frame:
            self.current_frame = frame
            self.current_frame_changed.emit(frame)
            self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            frame = long(fractions.Fraction(event.x()) / self.scale) + self.left_frame
            self.set_current_frame(frame)

    def mouseMoveEvent(self, event):
        frame = long(fractions.Fraction(event.x()) / self.scale) + self.left_frame
        self.set_current_frame(frame)

    def scale(self):
        return self.scale

    def set_scale(self, scale):
        '''
        Set the scale, in pixels per frame.

        '''
        self.scale = fractions.Fraction(scale)

        if len(self.ticks) < 3:
            self.minor_tick = None
            self.medium_tick = self.ticks[0]
            self.major_tick = self.ticks[-1]
        else:
            for minor, medium, major in zip(self.ticks[0:], self.ticks[1:], self.ticks[2:]):
                if fractions.Fraction(minor) * scale > TimeRuler.SMALL_TICK_THRESHOLD:
                    self.minor_tick, self.medium_tick, self.major_tick = minor, medium, major
                    break

        self.update()

    def set_timecode(self, timecode):
        self.timecode = timecode
        major_ticks = self.timecode.get_major_ticks()

        # Expand the major tick list with extra divisions
        last_tick = 1
        self.ticks = [1]

        for major_tick in major_ticks:
            for div in (10, 2):
                (divend, rem) = divmod(major_tick, div)

                if rem == 0 and divend > last_tick:
                    self.ticks.append(divend)

        self.update()

    def frame_to_pixel(self, frame):
        return float(fractions.Fraction(frame - self.left_frame) * self.scale) + 0.5

    def paintEvent(self, event):
        paint = QPainter(self)

        paint.setPen(QColor(0, 0, 0))

        major_ticks = self.timecode.get_major_ticks()

        start_frame = long(self.left_frame)
        width_frames = long(float(fractions.Fraction(self.width()) / self.scale))
        height = self.height()

        if self.minor_tick:
            for frame in range(start_frame - start_frame % self.minor_tick, start_frame + width_frames, self.minor_tick):
                x = self.frame_to_pixel(frame)
                paint.drawLine(x, height - 5, x, height)

        for frame in range(start_frame - start_frame % self.medium_tick, start_frame + width_frames, self.medium_tick):
            x = self.frame_to_pixel(frame)
            paint.drawLine(x, height - 10, x, height)

        for frame in range(start_frame - start_frame % self.major_tick, start_frame + width_frames, self.major_tick):
            x = self.frame_to_pixel(frame)
            paint.drawLine(x, height - 15, x, height)

        prev_right = None

        for frame in range(start_frame - start_frame % self.major_tick, start_frame + width_frames, self.major_tick):
            x = self.frame_to_pixel(frame)

            if prev_right is None or x > prev_right:
                text = self.timecode.format(frame)
                rect = paint.drawText(QRectF(), Qt.TextSingleLine, text)

                prev_right = x + rect.width() + 5.0
                paint.drawText(x + 2.5, 0.0, rect.width(), rect.height(), Qt.TextSingleLine, text)

        # Draw the pointer
        x = self.frame_to_pixel(self.current_frame)

        paint.setPen(Qt.NoPen)
        paint.setBrush(QColor.fromRgbF(1.0, 0.0, 0.0))
        paint.drawConvexPolygon(QPoint(x, height), QPoint(x + 5, height - 15), QPoint(x - 5, height - 15))

class TimelineScene(QGraphicsScene):
    frame_range_changed = pyqtSignal(int, int, name='frameRangeChanged')

    def __init__(self):
        QGraphicsScene.__init__(self)

    def update_frames(self, min_frame, max_frame):
        self.frame_range_changed.emit(min_frame, max_frame)

class TimelineView(QGraphicsView):
    black_pen = QPen(QColor.fromRgbF(0.0, 0.0, 0.0))
    white_pen = QPen(QColor.fromRgbF(1.0, 1.0, 1.0))

    def __init__(self, scene, clock):
        QGraphicsView.__init__(self, scene)
        self.setViewportMargins(0, 30, 0, 0)
        self.setAlignment(Qt.AlignLeft | Qt.AlignTop)

        self.ruler = TimeRuler(self, timecode=timecode.NtscDropFrame())
        self.ruler.move(self.frameWidth(), self.frameWidth())

        self.clock = clock
        self.frame_rate = fractions.Fraction(24000, 1001)

        self.white = False
        self.frame = 0
        self.set_current_frame(0)
        self.startTimer(1000)

        scene.sceneRectChanged.connect(self.handle_scene_rect_changed)
        scene.frame_range_changed.connect(self.handle_update_frames)
        self.ruler.current_frame_changed.connect(self.handle_ruler_current_frame_changed)

        self.scale_x = fractions.Fraction(1)
        self.scale_y = fractions.Fraction(1)

        self.scale(4, 1)

    def scale(self, sx, sy):
        self.scale_x = fractions.Fraction(sx)
        self.scale_y = fractions.Fraction(sy)

        self.ruler.set_scale(sx)

        self.resetTransform()
        QGraphicsView.scale(self, float(sx), float(sy))

    def set_current_frame(self, frame):
        '''
        view.set_current_frame(frame)

        Moves the view's current frame marker.
        '''
        self._invalidate_marker(self.frame)
        self.frame = frame
        self._invalidate_marker(frame)

        self.ruler.set_current_frame(frame)
        self.clock.seek(process.get_frame_time(self.frame_rate, int(frame)))

    def resizeEvent(self, event):
        self.ruler.resize(self.width() - self.frameWidth(), 30)

    def handle_scene_rect_changed(self, rect):
        left = self.mapToScene(0, 0).x()
        self.ruler.set_left_frame(left)

    def handle_update_frames(self, min_frame, max_frame):
        # If the current frame was in this set, re-seek to it
        # TODO: Verify that the clock isn't playing first
        if self.frame >= min_frame and self.frame <= max_frame:
            self.clock.seek(process.get_frame_time(self.frame_rate, int(self.frame)))

    def handle_ruler_current_frame_changed(self, frame):
        self.set_current_frame(frame)

    def updateSceneRect(self, rect):
        QGraphicsView.updateSceneRect(self, rect)

        left = self.mapToScene(0, 0).x()
        self.ruler.setLeftFrame(left)

    def scrollContentsBy(self, dx, dy):
        QGraphicsView.scrollContentsBy(self, dx, dy)

        if dx:
            left = self.mapToScene(0, 0).x()
            self.ruler.set_left_frame(left)

    def _invalidate_marker(self, frame):
        # BJC: No, for some reason, invalidateScene() did not work here
        self.scene().invalidate(QRectF(frame - 0.5, -20000.0, 1.0, 40000.0), QGraphicsScene.ForegroundLayer)

    def timerEvent(self, event):
        self.white = not self.white
        self._invalidate_marker(self.frame)

    def drawForeground(self, painter, rect):
        '''
        Draws the marker in the foreground.
        '''
        QGraphicsView.drawForeground(self, painter, rect)

        painter.setPen(self.white_pen if self.white else self.black_pen)
        painter.drawLine(self.frame, rect.y(), self.frame, rect.y() + rect.height())

class TimelineItem(QGraphicsItem):
    def __init__(self, item, name):
        QGraphicsItem.__init__(self)
        self.height = 30.0
        self._y = 0.0
        self.item = item
        self.name = name
        self.setPos(self.item.x, self._y)
        self.setFlags(QGraphicsItem.ItemIsMovable | QGraphicsItem.ItemIsSelectable)

    def boundingRect(self):
        return QRectF(0.0, 0.0, self.item.width, self.height)

    def paint(self, painter, option, widget):
        rect = painter.transform().mapRect(self.boundingRect())

        painter.save()
        painter.resetTransform()

        painter.fillRect(rect, QColor.fromRgbF(1.0, 0, 0) if self.isSelected() else QColor.fromRgbF(0.9, 0.9, 0.8))

        painter.setBrush(QColor.fromRgbF(0.0, 0.0, 0.0))
        painter.drawText(rect, Qt.TextSingleLine, self.name)

#        frame = self.item.source.get_frame_f16(0, (0, 0, 199, 199))
#        img_str = frame.to_argb32_string()

#        image = QImage(img_str, 200, 200, QImage.Format_ARGB32_Premultiplied)

#        painter.drawImage(rect.x(), rect.y(), image, sw=30, sh=30)

        painter.restore()

    def mouseMoveEvent(self, event):
        # There's a drag operation of some kind going on
        old_x = self.pos().x()

        QGraphicsItem.mouseMoveEvent(self, event)

        pos = self.pos()
        pos.setX(round(pos.x()))
        self.setPos(pos)

        self.scene().update_frames(min(old_x, pos.x()), max(old_x, pos.x()) + self.item.width - 1)
        self.item.update(x=int(pos.x()))

class MainWindow(QMainWindow):
    def __init__(self):
        QMainWindow.__init__(self)

        self.scene = TimelineScene()

        self.view = TimelineView(self.scene, clock)
        #self.view.setViewport(QGLWidget())
        self.view.setBackgroundBrush(QBrush(QColor.fromRgbF(0.5, 0.5, 0.5)))
        self.setCentralWidget(self.view)

        item = TimelineItem(workspace_item, 'Clip')
        self.scene.addItem(item)
        item.setSelected(True)

        format = QGLFormat()
        self.video_widget = qt.VideoWidget(format)
        self.video_dock = QDockWidget('Preview')
        self.video_dock.setWidget(self.video_widget)

        self.video_widget.setDisplayWindow((0, -1, 719, 478))

        self.video_widget.setPixelAspectRatio(640.0/704.0)
        self.video_widget.setPresentationClock(clock)
        self.video_widget.setVideoSource(workspace)

        clock.seek(0)

        self.addDockWidget(Qt.BottomDockWidgetArea, self.video_dock)

app = QApplication(sys.argv)

window = MainWindow()
window.show()

quit(app.exec_())

