#!/bin/bash
LD_LIBRARY_PATH=/home/james/software/extern/Mesa-8.0.2/build/linux-x86/gallium/targets/libgl-xlib python3 decode_raw_image.py ~/Videos/tape-2003-christmas-in-colorado.dv
#convert -size 720x480 -depth 8 r:plane_luma_dv plane_luma_dv.png
#convert -size 720x480 -depth 8 r:plane_luma_mpeg2 plane_luma_mpeg2.png
#convert -size 180x480 -depth 8 r:plane_cb_dv plane_cb_dv.png
#convert -size 360x240 -depth 8 r:plane_cb_mpeg2 plane_cb_mpeg2.png
#convert -size 180x480 -depth 8 r:plane_cr_dv plane_cr_dv.png
#convert -size 360x240 -depth 8 r:plane_cr_mpeg2 plane_cr_mpeg2.png
#eog plane_cb_dv.png &

