import distutils.sysconfig

debug = ARGUMENTS.get('debug', 0)
assembly = ARGUMENTS.get('assembly', 0)
profile = ARGUMENTS.get('profile', 0)
env = Environment(LIBS=['rt', 'GLEW'], CPPPATH=[distutils.sysconfig.get_python_inc()],
	SHLIBPREFIX='', CCFLAGS = ['-fno-strict-aliasing', '-std=c99', '-Wall', '-D_POSIX_C_SOURCE=200112L', '-fvisibility=hidden', '-fpic'])
	
env.Append(BUILDERS = {'PyGen': Builder(action = 'python $SOURCE > $TARGET')})
half = env.PyGen('halftab.c', 'genhalf.py')

if int(debug):
	env.Append(CCFLAGS = ['-ggdb3', '-DMESA_DEBUG', '-DDEBUG'])
elif int(profile):
	env.Append(CCFLAGS = ['-g'])
elif int(assembly):
	env.Append(CCFLAGS = ['-g', '-S', '-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno'])
else:
	env.Append(CCFLAGS = ['-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno', '-fno-tree-vectorize'])

env.ParseConfig('pkg-config --libs --cflags libavformat alsa OpenEXR libswscale gtk+-2.0 gl gtkglext-1.0 gthread-2.0 pygtk-2.0 pygobject-2.0')

lib = env.SharedLibrary('fluggo/media.so',
	['main.c',
		'FFVideoSource.c',
		'VideoSequence.c',
		'AudioSequence.c',
		'VideoMixFilter.c',
		'writeVideo.c',
		'FFAudioSource.c',
		'FFContainer.c',
		'GtkVideoWidget.c',
		'Pulldown23RemovalFilter.c',
		'AudioPassThroughFilter.c',
		'VideoPassThroughFilter.c',
		'clock.c',
		'half.c',
		'halftab.c',
		'AlsaPlayer.c',
		'basicframefuncs.c'])
Depends(lib, half)

