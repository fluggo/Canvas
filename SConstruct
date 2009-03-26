env = Environment(LIBS=['fltk', 'fltk_gl', 'rt'],
	CCFLAGS='-Wall -O3 -mtune=native -march=native')
env.ParseConfig('pkg-config --libs --cflags libavformat OpenEXR libswscale')

env.Program('test', ['test.cpp', 'AVFileReader.cpp', 'clock.cpp'])
