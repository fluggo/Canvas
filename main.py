# -*- coding: utf-8 -*-
from __future__ import unicode_literals
import glib
import gtk
import gtk.glade
from fractions import Fraction

from fluggo.media import process, timelines, sources, transitions
import fluggo.media.gtk

gtk.gdk.threads_init()

#clock = SystemPresentationClock()
audio = process.FFAudioSource('/home/james/Videos/Soft Boiled/Sources/BWFF/1B_1.wav')
player = process.AlsaPlayer(48000, source=audio)
clock = player

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
    widget = fluggo.media.gtk.VideoWidget(player)
    widget.drawing_area().show()

    # Temporary hack to keep the container object around
    widget.drawing_area().myobj = widget

    return widget.drawing_area()

def my_handler(glade, function_name, widget_name, str1, str2, int1, int2):
    return globals()[function_name]()

gtk.glade.set_custom_handler(my_handler)

class MainWindow(object):
    def __init__(self):
        self.glade = gtk.glade.XML('player.glade')
        self.glade.signal_autoconnect(self)
        self.video_widget = self.glade.get_widget('videoWidget').myobj
        self.video_widget.drawing_area().set_size_request(320, 240)
        self.frame_rate = Fraction(24000, 1001)
        self.frame_scale = self.glade.get_widget('frameScale')
        self.frame_scale.set_range(0, 5000)
        self.playing = False
        self.updating = False
        glib.timeout_add(100, self.update_current_frame)

        #av = AVFileReader('/home/james/Videos/Home Movies 2009-05-07-000-003.m2t')
        videro = process.FFVideoSource('/home/james/Videos/Soft Boiled/Sources/softboiled01;03;21;24.avi')
        pulldown = process.Pulldown23RemovalFilter(videro, 0);

        red = process.SolidColorVideoSource((1.0, 0.0, 0.0, 0.25), (20, 20, 318, 277))
        green = process.SolidColorVideoSource((0.0, 1.0, 0.0, 0.75), (200, 200, 518, 477))

        workspace = process.Workspace()
        workspace.add(source=pulldown, x=0, width=100, z=0)
        workspace.add(source=red, x=50, width=100, z=1)
        workspace.add(source=green, x=75, width=100, z=2)
        workspace.add(source=pulldown, x=125, width=100, z=0, offset=500)

        size = videro.size()
        self.video_widget.set_display_window((0, -1, size[0] - 1, size[1] - 2))
        #self.video_widget.set_hardware_accel(False)
        #self.video_widget.set_source(av)

        pulldown = process.Pulldown23RemovalFilter(videro, 0);
        seq = process.VideoSequence()
        seq.append((pulldown, 30, 60))
        seq.append((pulldown, 300, 250))
        seq.append((pulldown, 0, 150))

        #mix = VideoMixFilter(src_a=pulldown, src_b=EmptyVideoSource(), mix_b=LinearFrameFunc(a=1/300.0, b=0))
        #mix = VideoMixFilter(src_a=pulldown, src_b=pulldown, mix_b=LinearFrameFunc(a=1/300.0, b=0))
        #mix = VideoMixFilter(src_a=pulldown, src_b=SolidColorVideoSource((1.0, 0.0, 0.0, 0.5), (50, 50, 100, 100)), mix_b=LinearFrameFunc(a=1/300.0, b=0))
        #mix = VideoMixFilter(src_a=pulldown, src_b=seq, mix_b=LinearFrameFunc(a=1/300.0, b=0))
        mix = process.VideoMixFilter(src_a=process.SolidColorVideoSource((1.0, 0.0, 0.0, 0.25), (1, 0, 718, 477)), src_b=process.SolidColorVideoSource((0.0, 1.0, 0.0, 0.75), (2, 1, 717, 476)), mix_b=process.LinearFrameFunc(a=1/300.0, b=0))

        self.video_widget.set_source(workspace)
        clock.stop()

    def on_playButton_clicked(self, *args):
        clock.play(1)
        self.playing = True

    def on_rewindButton_clicked(self, *args):
        clock.play(-2)
        self.playing = True

    def on_forwardButton_clicked(self, *args):
        clock.play(2)
        self.playing = True

    def on_pauseButton_clicked(self, *args):
        clock.stop()
        self.update_current_frame()
        self.playing = False

    def on_frameScale_value_changed(self, control):
        if self.updating:
            return

        frame = int(control.get_value())
        time = process.get_frame_time(self.frame_rate, frame)
        #print frame, time

        clock.seek(time)

    def update_current_frame(self):
        self.updating = True
        frame = process.get_time_frame(self.frame_rate, clock.get_presentation_time())
        self.frame_scale.set_value(frame)
        self.updating = False
        return True

    def on_window1_destroy(self, *args):
        gtk.main_quit()

window = MainWindow()
gtk.main()



