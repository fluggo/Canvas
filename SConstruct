import distutils.sysconfig, os.path, os, sys
import fnmatch
import SCons.Defaults, SCons.Errors

# Recursive file match recipe: http://code.activestate.com/recipes/499305/
def locate(pattern, root=os.curdir):
    '''Locate all files matching supplied filename pattern in and below
    supplied root directory.'''
    for path, dirs, files in os.walk(os.path.abspath(root)):
        for filename in fnmatch.filter(files, pattern):
            yield os.path.join(path, filename)

# Utility function to find out what Python3 thinks
def python3_script(script):
    with open('.temp_python', 'w') as scriptfile:
        scriptfile.write(script)

    Execute('@python3 .temp_python > .temp_python_out')

    os.remove('.temp_python')
    result = None

    with open('.temp_python_out', 'r') as outfile:
        result = outfile.read(-1)

    os.remove('.temp_python_out')
    return result

debug = int(ARGUMENTS.get('debug', 0))
assembly = int(ARGUMENTS.get('assembly', 0))
profile = int(ARGUMENTS.get('profile', 0))
release = int(ARGUMENTS.get('release', 0))
mingw = int(ARGUMENTS.get('mingw', 0))

check_env = Environment()
tools = ['default']

if mingw or check_env['PLATFORM'] == 'win32':
    tools = ['mingw']

env = Environment(CPPPATH=['include'],
    CCFLAGS = ['-Wall', '-D_POSIX_C_SOURCE=200112L', '-Werror'],
    PYTHON=sys.executable, tools=tools)

msys_root = None

if mingw or env['PLATFORM'] == 'win32':
    # Find the MSYS root
    msys_root = os.path.dirname(os.path.dirname(env.WhereIs('gcc')))
    env.Append(CPPPATH=[os.path.join(msys_root, 'include\\w32api')],
        LIBPATH=[os.path.join(msys_root, 'lib\\w32api'), os.path.join(sys.exec_prefix, 'libs')],
        CCFLAGS=['-DWINNT'])
else:
    env.Append(CCFLAGS=['-fvisibility=hidden'])

env.Append(CFLAGS=['-std=c99'])

if debug:
    # Debug: Debug info + no optimizations
    env.Append(CCFLAGS = ['-ggdb3', '-DMESA_DEBUG', '-DDEBUG'])
elif release:
    # Release: No debug info + full optimizations + no asserts
    env.Append(CCFLAGS = ['-O3', '-mtune=native', '-march=native', '-fno-math-errno', '-DNDEBUG', '-DG_DISABLE_ASSERT'])
elif profile:
    # Profile: Test mode + no asserts (test mode is probably just fine for profiling)
    env.Append(CCFLAGS = ['-ggdb3', '-O3', '-mtune=native', '-march=native', '-fno-math-errno', '-DNDEBUG', '-DG_DISABLE_ASSERT'])
elif assembly:
    # Assembly: Produce assembler output
    env.Append(CCFLAGS = ['-S', '-O3', '-mtune=native', '-march=native', '-DNDEBUG', '-DG_DISABLE_ASSERT'])
else:
    # Test mode is the default: Debug info + full optimizations
    env.Append(CCFLAGS = ['-ggdb3', '-O3', '-mtune=native', '-march=native', '-fno-math-errno', '-DDEBUG'])

# Check to see if we can use clang
if tools[0] != 'mingw' and WhereIs('clang'):
    env['CC'] = 'clang'
elif not (debug or assembly):
    env.Append(CCFLAGS=['-fno-signed-zeros'])

# Set up a basic Python environment
python_include = python3_script('''
import distutils.sysconfig
print(distutils.sysconfig.get_python_inc())
''').strip()

python_env = env.Clone(SHLIBPREFIX='')
python_env.Append(CPPPATH=[python_include],
                  CCFLAGS=['-fno-strict-aliasing'],
                  LIBS=['python3.2mu'])

if mingw or env['PLATFORM'] == 'win32':
    python_env['SHLIBSUFFIX'] = '.pyd'
    python_env.Append(LIBS=['python26'], LINKFLAGS=['-Wl,--enable-auto-import'])

    # HACK: Work around a stupid bug in the SCons mingw tool
    python_env['WINDOWS_INSERT_DEF'] = 1

# Generate the half/float conversion tables
half = env.Command('src/cprocess/halftab.c', 'src/cprocess/genhalf.py', '$PYTHON $SOURCE > $TARGET')

# Build the cprocess shared objects
cprocess_env = env.Clone()
cprocess_env.ParseConfig('pkg-config --libs --cflags glib-2.0 gthread-2.0')

if env['PLATFORM'] == 'win32':
    cprocess_env.Append(LIBS=['glew32'])
else:
    cprocess_env.ParseConfig('pkg-config --libs --cflags gl')
    cprocess_env.Append(LIBS=['rt', 'GLEW'])

cprocess = [cprocess_env.SharedObject(None, node) for node in env.Glob('src/cprocess/*.c')]
Depends(cprocess, half)

# Build the process Python extension
process_env = python_env.Clone()
process_env.ParseConfig('pkg-config --libs --cflags glib-2.0 gthread-2.0')

if env['PLATFORM'] == 'win32':
    process_env.Append(LIBS=['glew32', 'opengl32'])
else:
    process_env.ParseConfig('pkg-config --libs --cflags gl')
    process_env.Append(LIBS=['rt', 'GLEW'])

process = process_env.SharedLibrary('fluggo/media/process', env.Glob('src/process/*.c') + cprocess)

if env['PLATFORM'] == 'win32':
    # Go back and narrow down the import lib
    process = [lib for lib in process if str(lib).endswith('.a')]

Alias('process', process)
Alias('all', 'process')
Default('process')

if not env.Execute('@pkg-config --exists libavformat libswscale'):
    libav_env = python_env.Clone()
    libav_env.ParseConfig('pkg-config --libs --cflags libavformat libswscale glib-2.0 gthread-2.0')
    libav_env.Append(LIBS=[process], CCFLAGS=['-Wno-error=deprecated-declarations'])

    if env['PLATFORM'] == 'win32':
        libav_env.Append(LIBS=['glew32', 'opengl32'])
    else:
        libav_env.ParseConfig('pkg-config --libs --cflags gl')
        libav_env.Append(LIBS=['GLEW'])

    libav = libav_env.SharedLibrary('fluggo/media/libav', env.Glob('src/libav/*.c'))

    Alias('libav', libav)
    Alias('all', 'libav')
    Default('libav')
else:
    print 'Skipping Libav library build'

if not env.Execute('@pkg-config --exists gtk+-2.0 gtkglext-1.0 pygtk-2.0 pygobject-2.0'):
    gtk_env = python_env.Clone()
    gtk_env.ParseConfig('pkg-config --libs --cflags gl gthread-2.0 gtk+-2.0 gtkglext-1.0 pygtk-2.0 pygobject-2.0')
    gtk_env.Append(LIBS=['GLEW', process])
    gtk = gtk_env.SharedLibrary('fluggo/media/gtk', ['src/gtk/GtkVideoWidget.c'])

    Alias('gtk', gtk)
    Alias('all', 'gtk')
    Default('gtk')
else:
    print 'Skipping GTK build'

if not env.Execute('@pkg-config --exists alsa'):
    alsa_env = python_env.Clone()
    alsa_env.ParseConfig('pkg-config --libs --cflags alsa glib-2.0 gthread-2.0')
    alsa_env.Append(LIBS=[process])
    alsa = alsa_env.SharedLibrary('fluggo/media/alsa', alsa_env.Glob('src/alsa/*.c'))

    Alias('alsa', alsa)
    Alias('all', 'alsa')
    Default('alsa')
else:
    print 'Skipping ALSA build'

try:
    (sip_bin, pyqt_sip_dir, pyqt_sip_flags) = python3_script('''
import PyQt4.pyqtconfig
config = PyQt4.pyqtconfig.Configuration()

print(config.sip_bin)
print(config.pyqt_sip_dir)
print(config.pyqt_sip_flags)
''').splitlines()

    # TODO: Separate this out as a tool

    qt_env = python_env.Clone()

    # Get type system includes
    def get_pkg_config(module, variable):
        my_env = env.Clone()
        my_env['MODNAME'] = module
        my_env['VARNAME'] = variable
        my_env.Execute('@pkg-config --variable=$VARNAME $MODNAME > .temppkgconfig')

        result = None

        with open('.temppkgconfig', 'r') as outfile:
            result = outfile.read(-1)

        os.remove('.temppkgconfig')
        return result.strip()

    # We need to publish PyInit_qt, let sip handle it
    qt_env['CCFLAGS'].remove('-fvisibility=hidden')

    # These flags *would* come from "pkg-config --cflags pyside"
    # except that on my system, that includes /usr/include/python2.7
    qt_env.Append(CPPPATH='/usr/include/shiboken')
    qt_env.Append(CPPPATH='/usr/include/PySide')
    qt_env.Append(CPPPATH='/usr/include/PySide/QtCore')
    qt_env.Append(CPPPATH='/usr/include/PySide/QtGui')
    qt_env.Append(CPPPATH='/usr/include/PySide/QtOpenGL')
    qt_env.Append(CCFLAGS='-DBUILD_SHIBOKEN')
    qt_env.Append(CCFLAGS='-frtti')
    qt_env.Append(CPPPATH='/usr/include')
    qt_env.Append(LIBS=['pyside.cpython-32mu', 'shiboken.cpython-32mu'])

    qt_env['GENERATORRUNNER'] = qt_env.WhereIs('generatorrunner')
    qt_env['GENERATORRUNNERINC'] = ['src/qt', 'include', get_pkg_config('pyside', 'includedir'),
        get_pkg_config('QtCore', 'includedir'), get_pkg_config('QtGui', 'includedir'),
        get_pkg_config('QtOpenGL', 'includedir'), '/usr/include/qt4', '/usr/include']
    qt_env['TYPESYSTEMINC'] = ['.', get_pkg_config('pyside', 'typesystemdir')]
    qt_env['SHIBOKENOUTDIR'] = 'build/qt'
    qt_env['SHIBOKENCOM'] = ('$GENERATORRUNNER --generatorSet=shiboken ' +
        "--include-paths=${':'.join(GENERATORRUNNERINC)} " +
        "--typesystem-paths=${':'.join(TYPESYSTEMINC)} " +
        '--output-directory=build/qt ' +
        '${SOURCES[0]} ${SOURCES[1]}')
    gr_action = Action('$SHIBOKENCOM', '$SHIBOKENCOMSTR')

    def emitter(target, source, env):
        module = str(target[0])

        gr_targets = [env.File('$SHIBOKENOUTDIR/' + module + '/' + module + '_module_wrapper.cpp')]
        module_sources = []
        headers = [env.File('$SHIBOKENOUTDIR/' + module + '/' + module + '_python.h')]

        for s in source[2:]:
            (root, ext) = os.path.splitext(os.path.basename(str(s)))

            file = env.File('$SHIBOKENOUTDIR/' + module + '/' + root.lower() + '_wrapper.cpp')
            gr_targets.append(file)
            headers.append(env.File('$SHIBOKENOUTDIR/' + module + '/' + root.lower() + '_wrapper.h'))

        env.SideEffect(headers, gr_targets)
        env.Clean(gr_targets, headers)

        return (gr_targets, source)

    gr_builder = Builder(action=gr_action, emitter=emitter, source_scanner=SCons.Defaults.CScan)

    qt_env.Append(BUILDERS={'ShibokenModule': gr_builder}, CPPPATH=['src/qt'], LIBS=[process])
    qt_env.ParseConfig('pkg-config --libs --cflags QtGui QtOpenGL gl glib-2.0')

    # BJC: Doing this in the qt_env adds a weird -lqt to the proceedings,
    # and even appears to add moc_VideoWidget.os twice; this gets us what
    # we want
    moc_env = qt_env.Clone(tools=['default', 'qt'])
    moc = moc_env.Moc('src/qt/VideoWidget.h')

    qt_sip = qt_env.ShibokenModule('qt', ['src/qt/global.h', 'src/qt/sidetypes.xml', 'src/qt/VideoWidget.h'])
    qt = qt_env.SharedLibrary('fluggo/media/qt.so', qt_sip + moc + env.Glob('src/qt/*.cpp'))

    qt_env.Clean(qt, 'build/qt')

    Alias('qt', qt)
    Alias('all', 'qt')
    Default(qt)
except Exception as ex:
    print 'Skipping Qt4 build: ' + str(ex)

# Tests
testenv = env.Clone()
testenv.Append(ENV={'PYTHONPATH': env.Dir('.')}, LIBS=[cprocess, 'GLEW', 'm', 'rt'])
if env['PLATFORM'] != 'win32':
    testenv.ParseConfig('pkg-config --libs --cflags gl glib-2.0 gthread-2.0')

test_cprocess = testenv.Program('tests/cprocess_test', env.Glob('src/tests/*.c'))
testenv.Alias('test', testenv.Command('test_dummy', 'tests/cprocess_test', '@tests/cprocess_test'))

for testfile in locate('*.py', 'tests'):
    testenv.Alias('test', testenv.Command(None, testfile, '@python3 testrunner.py $SOURCE'))

Requires('test', cprocess)
Requires('test', test_cprocess)
Requires('test', process)
Alias('all', 'test')

# Documentation
def ensure_sphinx_ver(target, source, env):
    if not env.WhereIs('sphinx-build'):
        raise SCons.Errors.StopError('Could not find sphinx-build')

    return 0

node_list = []

env.Command('docs/html/index.html', File(list(locate('*.rst', 'docs/sphinx'))) + File(list(locate('*.py', 'fluggo'))), '@sphinx-build -b html docs/sphinx docs/html')
env.AddPreAction('docs/html/index.html', env.Action(ensure_sphinx_ver, 'Checking Sphinx version...'))
Alias('docs', 'docs/html/index.html')
Clean('docs', Glob('docs/html/*'))

Alias('all', 'docs')



