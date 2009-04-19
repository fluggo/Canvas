
import gtk
from fluggo.video import *

clock = SystemPresentationClock()

window = gtk.Window(gtk.WINDOW_TOPLEVEL)
window.set_title('boogidy boogidy')

clock.set((1, 1), 5000L * 1000000000L * 1001L / 24000L)

widget = VideoWidget(clock, AVFileReader('/home/james/Videos/Okra - 79b,100.avi'))
drawingArea = widget.drawingArea()

#window.connect( G_OBJECT(window), "key-press-event", G_CALLBACK(keyPressHandler), drawingArea );
window.connect('delete_event', gtk.main_quit)

window.add(drawingArea)
drawingArea.show()

window.show()

gtk.main()



