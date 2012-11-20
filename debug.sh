#!/bin/bash
LD_LIBRARY_PATH=/home/james/software/extern/Mesa-8.0.2/build/linux-x86/gallium/targets/libgl-xlib gdb --args python3 qttest.py --log=debug --break-exc
