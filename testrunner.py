

import sys
import os.path
import unittest

# Grab command-line arguments
import argparse

argparser = argparse.ArgumentParser()
argparser.add_argument('path', help='Path to the test script.')
argparser.add_argument('testnames', metavar='testname', nargs='*',
    help='Optional name of a test to run. This can be a class name or class and method.')
argparser.add_argument('--log-level', dest='log_level',
                       choices=['debug', 'info', 'warning', 'error', 'critical'],
                       default='warning',
                       help='Logging level to use for the root logger.')
argparser.add_argument('--log', dest='log_filter', action='append',
                       help='Log debug events from the given module.',
                       default=[])
argparser.add_argument('--log-glib', dest='enable_glib_logging', action='store_true',
                       help='Enable event logging from GLib and C processing libraries.', default=False)

args = argparser.parse_args()

# Set up logging
if args.enable_glib_logging:
    import fluggo.media.process
    fluggo.media.process.enable_glib_logging(args.enable_glib_logging)

if True:
    import fluggo.logging
    import logging
    handler = logging.StreamHandler()
    handler.setLevel(logging.NOTSET)
    handler.setFormatter(logging.Formatter('{levelname}:{name}:{msg}', style='{'))

    root_logger = logging.getLogger()
    root_logger.setLevel(logging.NOTSET)
    root_logger.addHandler(handler)

    rootno = getattr(logging, args.log_level.upper())

    filtereq = [arg for arg in args.log_filter]
    filterstart = [arg + '.' for arg in args.log_filter]

    class MyFilter:
        def filter(self, record):
            for name in filtereq:
                if name == record.name:
                    return True

            for name in filterstart:
                if record.name.startswith(name):
                    return True

            if record.levelno >= rootno:
                return True

            return False

    handler.addFilter(MyFilter())



path = args.path
names = args.testnames

sys.path.insert(0, os.path.dirname(path))

testmodule = __import__(os.path.basename(path)[:-3])

if __name__ == '__main__':
    print('Running tests from {0}...'.format(path[:-3]), end='')
    sys.stdout.flush()

    suite = None

    if names:
        suite = unittest.defaultTestLoader.loadTestsFromNames(names, testmodule)
    else:
        suite = unittest.defaultTestLoader.loadTestsFromModule(testmodule)

    result = unittest.TestResult()
    suite.run(result)

    if len(result.errors):
        print('ERROR')

        for test, traceback in result.errors:
            print('--> ' + str(test))
            print(traceback)

        if len(result.failures):
            print('\nAlso, failures:')

            for test, traceback in result.failures:
                print('--> ' + str(test))
                print(traceback)

        sys.exit(1)
    elif len(result.failures):
        print('FAIL')

        for test, traceback in result.failures:
            print('--> ' + str(test))
            print(traceback)

        sys.exit(1)
    else:
        print('PASS')


