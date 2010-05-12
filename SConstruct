import distutils.sysconfig, os.path
import SCons.Defaults

debug = ARGUMENTS.get('debug', 0)
assembly = ARGUMENTS.get('assembly', 0)
profile = ARGUMENTS.get('profile', 0)

env = Environment(CPPPATH=['include'],
    CCFLAGS = ['-fno-strict-aliasing', '-Wall', '-D_POSIX_C_SOURCE=200112L', '-Werror'],
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
process = process_env.SharedLibrary('fluggo/media/process.so', env.Glob('src/process/*.c') + env.Glob('src/pyprocess/*.c'))
Depends(process, half)

Alias('process', process)
Alias('all', 'process')
Default('process')

gtk_env = python_env.Clone()
gtk_env.ParseConfig('pkg-config --libs --cflags gl gthread-2.0 gtk+-2.0 gtkglext-1.0 pygtk-2.0 pygobject-2.0')
gtk_env.Append(LIBS=['GLEW', process], CCFLAGS=['-fvisibility=hidden'])
gtk = gtk_env.SharedLibrary('fluggo/media/gtk.so', ['src/gtk/GtkVideoWidget.c'])

Alias('gtk', gtk)
Alias('all', 'gtk')
Default('gtk')

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
        module = str(target[0])

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

        return (sip_targets, sip_sources)

    sip_builder = Builder(action=sip_action, emitter=emitter, source_scanner=SCons.Defaults.CScan)

    qt_env.Append(BUILDERS={'SipModule': sip_builder}, CPPPATH=['src/qt'], LIBS=[process])
    qt_env.ParseConfig('pkg-config --libs --cflags QtGui QtOpenGL gl glib-2.0')

    qt_sip = qt_env.SipModule('qt', env.Glob('src/qt/*.sip') + env.Glob('src/qt/*.cpp'))
    qt = qt_env.SharedLibrary('fluggo/media/qt.so', qt_sip)

    qt_env.Clean(qt, 'build/qt')

    Alias('qt', qt)
    Alias('all', 'qt')
    Default(qt)

# Tests
testenv = env.Clone()
testenv.Append(ENV={'PYTHONPATH': env.Dir('.')})

for testfile in Glob('tests/test_*.py'):
    testenv.Alias('test', testenv.Command(None, testfile, '@python tests/testrunner.py $SOURCE.filebase'))

Requires('test', process)
Alias('all', 'test')

# Documentation
if env.WhereIs('naturaldocs'):
    Command('docs/html/index.html', Glob('src/process/*.c'), '@naturaldocs -i src -o HTML docs/html -p docs/natural -ro')
    Alias('docs', 'docs/html/index.html')
    Clean('docs', Glob('docs/html/*'))
    Alias('all', 'docs')

