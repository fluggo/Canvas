import distutils.sysconfig, os.path
import SCons.Defaults

debug = ARGUMENTS.get('debug', 0)
assembly = ARGUMENTS.get('assembly', 0)
profile = ARGUMENTS.get('profile', 0)
test = ARGUMENTS.get('test', 0)

env = Environment(CPPPATH=['include'],
    CCFLAGS = ['-Wall', '-D_POSIX_C_SOURCE=200112L', '-Werror'],
    CFLAGS=['-std=c99'])

# Check to see if we can use clang
if WhereIs('clang'):
    env['CC'] = 'clang'

if int(debug):
    env.Append(CCFLAGS = ['-ggdb3', '-DMESA_DEBUG', '-DDEBUG'])
elif int(profile):
    env.Append(CCFLAGS = ['-ggdb3', '-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno', '-DNDEBUG', '-DG_DISABLE_ASSERT'])
elif int(test):
    env.Append(CCFLAGS = ['-ggdb3', '-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno', '-DDEBUG'])
elif int(assembly):
    env.Append(CCFLAGS = ['-g', '-S', '-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno'])
else:
    env.Append(CCFLAGS = ['-O3', '-mtune=native', '-march=native', '-fno-signed-zeros', '-fno-math-errno', '-DNDEBUG', '-DG_DISABLE_ASSERT'])

python_env = env.Clone(SHLIBPREFIX='')
python_env.Append(CPPPATH=[distutils.sysconfig.get_python_inc()], CCFLAGS=['-fno-strict-aliasing'])

half = Command('src/cprocess/halftab.c', 'src/cprocess/genhalf.py', 'python $SOURCE > $TARGET')

cprocess_env = env.Clone()
cprocess_env.ParseConfig('pkg-config --libs --cflags gl glib-2.0 gthread-2.0')
cprocess_env.Append(LIBS=['rt', 'GLEW'], CCFLAGS=['-fvisibility=hidden'])

cprocess = [cprocess_env.SharedObject(None, node) for node in env.Glob('src/cprocess/*.c')]
Depends(cprocess, half)

process_env = python_env.Clone()
process_env.ParseConfig('pkg-config --libs --cflags libavformat libswscale alsa gl glib-2.0 gthread-2.0')
process_env.Append(LIBS=['rt', 'GLEW'], CCFLAGS=['-fvisibility=hidden'])
process = process_env.SharedLibrary('fluggo/media/process.so', env.Glob('src/process/*.c') + cprocess)

Alias('process', process)
Alias('all', 'process')
Default('process')

if not Execute('@pkg-config --exists gtk+-2.0 gtkglext-1.0 pygtk-2.0 pygobject-2.0'):
    gtk_env = python_env.Clone()
    gtk_env.ParseConfig('pkg-config --libs --cflags gl gthread-2.0 gtk+-2.0 gtkglext-1.0 pygtk-2.0 pygobject-2.0')
    gtk_env.Append(LIBS=['GLEW', process], CCFLAGS=['-fvisibility=hidden'])
    gtk = gtk_env.SharedLibrary('fluggo/media/gtk.so', ['src/gtk/GtkVideoWidget.c'])

    Alias('gtk', gtk)
    Alias('all', 'gtk')
    Default('gtk')
else:
    print 'Skipping GTK build'

try:
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

            file = env.File('$SIPSRCDIR/sip' + module + root + '.cpp')
            sip_targets.append(file)
            sip_sources.append(s)

        header = '$SIPSRCDIR/sipAPI' + module + '.h'
        env.SideEffect(header, sip_targets)
        env.Clean(sip_targets, header)

        return (sip_targets, sip_sources)

    sip_builder = Builder(action=sip_action, emitter=emitter, source_scanner=SCons.Defaults.CScan)

    qt_env.Append(BUILDERS={'SipModule': sip_builder}, CPPPATH=['src/qt'], LIBS=[process])
    qt_env.ParseConfig('pkg-config --libs --cflags QtGui QtOpenGL gl glib-2.0')

    qt_sip = qt_env.SipModule('qt', env.Glob('src/qt/*.sip'))
    qt = qt_env.SharedLibrary('fluggo/media/qt.so', qt_sip + env.Glob('src/qt/*.cpp'))

    qt_env.Clean(qt, 'build/qt')

    Alias('qt', qt)
    Alias('all', 'qt')
    Default(qt)
except Exception as ex:
    print 'Skipping Qt4 build: ' + str(ex)

# Tests
testenv = env.Clone()
testenv.Append(ENV={'PYTHONPATH': env.Dir('.')})

for testfile in Glob('tests/test_*.py'):
    testenv.Alias('test', testenv.Command(None, testfile, '@python tests/testrunner.py $SOURCE.filebase'))

Requires('test', process)
Alias('all', 'test')

# Documentation
if env.WhereIs('naturaldocs'):
    Command('docs/html/index.html', Glob('src/cprocess/*.c') + Glob('src/gtk/*.c') + Glob('src/process/*.c'), '@naturaldocs -i src -o HTML docs/html -p docs/natural -ro')
    Alias('docs', 'docs/html/index.html')
    Clean('docs', Glob('docs/html/*'))
    Alias('all', 'docs')

