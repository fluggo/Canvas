import distutils.sysconfig

debug = ARGUMENTS.get('debug', 0)
assembly = ARGUMENTS.get('assembly', 0)
profile = ARGUMENTS.get('profile', 0)
env = Environment(LIBS=['rt', 'GLEW'], CPPPATH=[distutils.sysconfig.get_python_inc()],
	SHLIBPREFIX='')
	
env.Append(BUILDERS = {'PyGen': Builder(action = 'python $SOURCE > $TARGET')})
half = env.PyGen('half.c', 'genhalf.py')

if int(debug):
	env['CCFLAGS'] = '-fno-strict-aliasing -std=c99 -Wall -ggdb3 -DMESA_DEBUG -DDEBUG'
elif int(profile):
	env['CCFLAGS'] = '-fno-strict-aliasing -std=c99 -Wall -g'
elif int(assembly):
	env['CCFLAGS'] = '-fno-strict-aliasing -std=c99 -g -S -Wall -O3 -mtune=native -march=native -fno-signed-zeros -fno-math-errno'
else:
	env['CCFLAGS'] = '-fno-strict-aliasing -std=c99 -Wall -O3 -mtune=native -march=native -fno-signed-zeros -fno-math-errno'

env.ParseConfig('pkg-config --libs --cflags libavformat alsa OpenEXR libswscale gtk+-2.0 gl gtkglext-1.0 gthread-2.0 pygtk-2.0 pygobject-2.0')

lib = env.SharedLibrary('fluggo/video.so',
	['test.c', 'AVFileReader.c', 'Pulldown23RemovalFilter.c', 'clock.c', 'half.c', 'AlsaPlayer.c'])
Depends(lib, half)

