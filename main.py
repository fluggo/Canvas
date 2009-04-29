
import gtk
import gtk.glade
from fluggo.video import *

clock = SystemPresentationClock()

#window = gtk.Window(gtk.WINDOW_TOPLEVEL)
#window.set_title('boogidy boogidy')

#clock.set((1, 1), 5000L * 1000000000L * 1001L / 24000L)

#widget = VideoWidget(clock,
#    Pulldown23RemovalFilter(
#    AVFileReader('/home/james/Videos/Okra - 79b,100.avi'), 0, False ) )
#drawingArea = widget.drawingArea()

#window.connect( G_OBJECT(window), "key-press-event", G_CALLBACK(keyPressHandler), drawingArea );
#window.connect('delete_event', gtk.main_quit)

#window.add(drawingArea)
#drawingArea.show()

#window.show()

def createVideoWidget():
    widget = VideoWidget(clock,
        Pulldown23RemovalFilter(
        AVFileReader('/home/james/Videos/Okra - 79b,100.avi'), 0, False ) )

    widget.drawingArea().show()

    # Temporary hack to keep the container object around
    widget.drawingArea().myobj = widget

    return widget.drawingArea()

def my_handler(glade, function_name, widget_name, str1, str2, int1, int2):
    return globals()[function_name]()

gtk.glade.set_custom_handler(my_handler)

class MainWindow(object):
    def __init__(self):
        self.glade = gtk.glade.XML('player.glade')
        self.glade.signal_autoconnect(self)
        self.videoWidget = self.glade.get_widget('videoWidget').myobj
        self.videoWidget.stop()
        self.frameRate = Rational(24000, 1001)
        self.glade.get_widget('frameScale').set_range(0, 5000)

    def on_playButton_clicked(self, *args):
        clock.play((1, 1))
        self.videoWidget.play()

    def on_rewindButton_clicked(self, *args):
        clock.play((-2, 1))
        self.videoWidget.play()

    def on_forwardButton_clicked(self, *args):
        clock.play((2, 1))
        self.videoWidget.play()

    def on_pauseButton_clicked(self, *args):
        clock.play((0, 1))
        self.videoWidget.stop()

    def on_frameScale_value_changed(self, control):
        frame = int(control.get_value())
        time = getFrameTime(self.frameRate, frame)
        #print frame, time

        clock.seek(time)
        self.videoWidget.stop()

    def on_window1_destroy(self, *args):
        gtk.main_quit()

window = MainWindow()
gtk.main()



