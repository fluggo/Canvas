debug = ARGUMENTS.get('debug', 0)


env = Environment(LIBS=['rt', 'GLEW'])

if int(debug):
	env['CCFLAGS'] = '-Wall -ggdb3 -DMESA_DEBUG -DDEBUG'
else:
	env['CCFLAGS'] = '-Wall -O3 -mtune=native -march=native'

env.ParseConfig('pkg-config --libs --cflags libavformat OpenEXR libswscale gtk+-2.0 gl gtkglext-1.0 gthread-2.0')

env.Program('test', ['test.cpp', 'AVFileReader.cpp', 'clock.cpp'])
