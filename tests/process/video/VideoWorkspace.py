import unittest, random
from fluggo.media import process
from fluggo.media.basetypes import *

red = process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (100, 0, 0, 1), 100))
green = process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (0, 100, 0, 1), 100))
blue = process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (0, 0, 100, 1), 100))

def getcolor(source, frame):
    return source.get_frame_f32(frame, box2i(0, 0, 0, 0)).pixel(0, 0)

class test_VideoWorkspace(unittest.TestCase):
    def test_random(self):
        workspace = process.VideoWorkspace()

        def randaction(action):
            if action == 1 and len(workspace):
                random.choice(workspace).update(x=random.randint(0, 1000))
            elif action == 2 and len(workspace):
                random.choice(workspace).update(z=random.randint(-10, 10))
            elif action == 3 and len(workspace):
                random.choice(workspace).update(length=random.randint(1, 100))
            elif action == 4 and len(workspace):
                random.choice(workspace).update(offset=random.randint(-20, 20))
            elif action == 5 and len(workspace):
                workspace.remove(random.choice(workspace))
            elif action == 6:
                for i in range(10):
                    getcolor(workspace, random.randint(-100, 1100))
            else:
                workspace.add(source=random.choice((red, green, blue)),
                    x=random.randint(0, 1000),
                    z=random.randint(-10, 10),
                    length=random.randint(1, 100),
                    offset=random.randint(-20, 20))

        for i in range(10000):
            randaction(random.randint(1,7))

