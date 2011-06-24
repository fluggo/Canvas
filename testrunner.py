from __future__ import print_function

import sys
import os.path
import unittest

path = sys.argv[1]
names = sys.argv[2:]

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


