import distutils.sysconfig

debug = ARGUMENTS.get('debug', 0)
profile = ARGUMENTS.get('profile', 0)
env = Environment(LIBS=['rt', 'GLEW'], CPPPATH=[distutils.sysconfig.get_python_inc()],
	SHLIBPREFIX='')

if int(debug):
	env['CCFLAGS'] = '-Wall -ggdb3 -DMESA_DEBUG -DDEBUG'
elif int(profile):
	env['CCFLAGS'] = '-Wall -g'
else:
	env['CCFLAGS'] = '-Wall -O3 -mtune=native -march=native'

env.ParseConfig('pkg-config --libs --cflags libavformat OpenEXR libswscale gtk+-2.0 gl gtkglext-1.0 gthread-2.0 pygtk-2.0 pygobject-2.0')

env.SharedLibrary('fluggo/video.so',
	['test.cpp', 'AVFileReader.cpp', 'Pulldown23RemovalFilter.cpp', 'clock.cpp'])
