import distutils.sysconfig

debug = ARGUMENTS.get('debug', 0)
assembly = ARGUMENTS.get('assembly', 0)
profile = ARGUMENTS.get('profile', 0)
env = Environment(CPPPATH=[distutils.sysconfig.get_python_inc(), 'include'],
	SHLIBPREFIX='',
	CCFLAGS = ['-fno-strict-aliasing', '-std=c99', '-Wall', '-D_POSIX_C_SOURCE=200112L', '-fvisibility=hidden'])

half = Command('src/process/halftab.c', 'src/process/genhalf.py', 'python $SOURCE > $TARGET')

if int(debug):
	env.Append(CCFLAGS = ['-ggdb3', '-DMESA_DEBUG', '-DDEBUG'])
elif int(profile):
	env.Append(CCFLAGS = ['-g'])
elif int(assembly):
	env.Append(CCFLAGS = ['-g', '-S', '-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno'])
else:
	env.Append(CCFLAGS = ['-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno', '-fno-tree-vectorize'])

process_env = env.Clone()
process_env.ParseConfig('pkg-config --libs --cflags libavformat alsa OpenEXR libswscale gl glib-2.0')
process_env.Append(LIBS=['rt', 'GLEW'])
process = process_env.SharedLibrary('fluggo/media/process.so', env.Glob('src/process/*.c'))
Depends(process, half)

gtk_env = env.Clone()
gtk_env.ParseConfig('pkg-config --libs --cflags gl gthread-2.0 gtk+-2.0 gtkglext-1.0 pygtk-2.0 pygobject-2.0')
gtk_env.Append(LIBS=['GLEW', process])
gtk = gtk_env.SharedLibrary('fluggo/media/gtk.so', ['src/gtk/GtkVideoWidget.c'])


