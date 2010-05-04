from __future__ import print_function

import sys
import unittest

testmodule = __import__(sys.argv[1])

if __name__ == '__main__':
    print('Running tests from {0}...'.format(testmodule.__name__), end='')

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


