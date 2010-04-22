import distutils.sysconfig, os.path
import SCons.Defaults

debug = ARGUMENTS.get('debug', 0)
assembly = ARGUMENTS.get('assembly', 0)
profile = ARGUMENTS.get('profile', 0)

env = Environment(CPPPATH=['include'],
	CCFLAGS = ['-fno-strict-aliasing', '-Wall', '-D_POSIX_C_SOURCE=200112L', '-Werror=implicit-function-declaration', '-Werror=implicit-int'],
	CFLAGS=['-std=c99'])

if int(debug):
	env.Append(CCFLAGS = ['-ggdb3', '-DMESA_DEBUG', '-DDEBUG'])
elif int(profile):
	env.Append(CCFLAGS = ['-g'])
elif int(assembly):
	env.Append(CCFLAGS = ['-g', '-S', '-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno'])
else:
	env.Append(CCFLAGS = ['-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno', '-fno-tree-vectorize'])

python_env = env.Clone(SHLIBPREFIX='')
python_env.Append(CPPPATH=[distutils.sysconfig.get_python_inc()])

half = Command('src/process/halftab.c', 'src/process/genhalf.py', 'python $SOURCE > $TARGET')

process_env = python_env.Clone()
process_env.ParseConfig('pkg-config --libs --cflags libavformat alsa OpenEXR libswscale gl glib-2.0 gthread-2.0')
process_env.Append(LIBS=['rt', 'GLEW'], CCFLAGS=['-fvisibility=hidden'])
process = process_env.SharedLibrary('fluggo/media/process.so', env.Glob('src/process/*.c'))
Depends(process, half)

gtk_env = python_env.Clone()
gtk_env.ParseConfig('pkg-config --libs --cflags gl gthread-2.0 gtk+-2.0 gtkglext-1.0 pygtk-2.0 pygobject-2.0')
gtk_env.Append(LIBS=['GLEW', process], CCFLAGS=['-fvisibility=hidden'])
gtk = gtk_env.SharedLibrary('fluggo/media/gtk.so', ['src/gtk/GtkVideoWidget.c'])

if True:
	import PyQt4.pyqtconfig
	config = PyQt4.pyqtconfig.Configuration()

	# TODO: Separate this out as a tool

	qt_env = python_env.Clone(tools=['default', 'qt4'], toolpath=['tools'])

	qt_env['SIP'] = config.sip_bin
	qt_env['SIPINCLUDE'] = config.pyqt_sip_dir
	qt_env['SIPFLAGS'] = config.pyqt_sip_flags
	qt_env['SIPCOM'] = '$SIP -I $SIPINCLUDE -c $SIPSRCDIR $SIPFLAGS $SOURCES'
	qt_env['SIPSRCDIR'] = 'build/qt/sip'
	sip_action = Action('$SIPCOM', '$SIPCOMSTR')

	def emitter(target, source, env):
		(target_dir, module) = os.path.split(str(target[0]))
		(module, tmp) = os.path.splitext(module)

		sip_targets = [env.File('$SIPSRCDIR/sip' + module + 'cmodule.cpp')]
		sip_sources = []
		module_sources = []

		for s in source:
			(root, ext) = os.path.splitext(os.path.basename(str(s)))

			if ext == '.sip':
				file = env.File('$SIPSRCDIR/sip' + module + root + '.cpp')
				sip_targets.append(file)
				sip_sources.append(s)
			else:
				module_sources.append(s)

		module_sources.extend(sip_targets)

		header = '$SIPSRCDIR/sipAPI' + module + '.h'
		env.SideEffect(header, sip_targets)
		env.Clean(sip_targets, header)

		env.SharedLibrary(target[0], module_sources)

		return (sip_targets, sip_sources)

	sip_builder = Builder(action=sip_action, emitter=emitter, source_scanner=SCons.Defaults.CScan)

	qt_env.Append(BUILDERS={'SipModule': sip_builder}, CPPPATH=['src/qt'], LIBS=[process])
	qt_env.ParseConfig('pkg-config --libs --cflags QtGui QtOpenGL gl glib-2.0')

	Execute(Mkdir('build/qt/sip'))
	qt_env.Clean('build/qt/sip', Dir('build/qt/sip'))
	qt = qt_env.SipModule('fluggo/media/qt.so', env.Glob('src/qt/*.sip') + env.Glob('src/qt/*.cpp'))

