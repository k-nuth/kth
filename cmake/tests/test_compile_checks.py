#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regressions for the actual CMake response files and compile-test driver."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'src/infrastructure/test/compile_fail/test_compile_failures.py'
spec = importlib.util.spec_from_file_location('compile_checks', SCRIPT)
driver = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = driver
spec.loader.exec_module(driver)


class DriverFailures(unittest.TestCase):
    def rejected(self):
        return [driver.CompileFailTest('invalid', '"abc"_base16', 'odd length')]

    def test_timeout_is_not_a_successful_rejection(self):
        with patch.object(driver.subprocess, 'run', side_effect=subprocess.TimeoutExpired('c++', 30)):
            passed, failed = driver.run_tests(self.rejected(), [], 'c++', '/unused')
        self.assertEqual((passed, failed), (0, 1))

    def test_compiler_crash_is_not_a_successful_rejection(self):
        crashed = subprocess.CompletedProcess(['c++'], -11, '', 'compiler crashed')
        with patch.object(driver.subprocess, 'run', return_value=crashed):
            passed, failed = driver.run_tests(self.rejected(), [], 'c++', '/unused')
        self.assertEqual((passed, failed), (0, 1))

    def test_failed_positive_control_stops_negative_cases(self):
        positive = [driver.CompileFailTest('valid', '"00"_base16', 'valid literal')]
        with patch.object(driver, 'try_compile', return_value=(False, 'missing algorithm header')) as compiler:
            passed, failed = driver.run_tests(self.rejected(), positive, 'c++', '/unused')
        self.assertEqual((passed, failed), (0, 1))
        self.assertEqual(compiler.call_count, 1)


class ConfiguredToolchain(unittest.TestCase):
    def test_empty_evaluated_options_are_not_compiler_inputs(self):
        cmake = os.environ.get('CMAKE_COMMAND') or shutil.which('cmake')
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='empty-compile-options-') as temp:
            root = Path(temp)
            (root / 'control.cpp').write_text('int control() { return 0; }\n')
            (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(empty_options LANGUAGES CXX)
add_library(dependency INTERFACE)
target_compile_options(dependency INTERFACE
    "$<$<CONFIG:Release>:$<$<COMPILE_LANGUAGE:CXX>:>>"
    "$<$<CONFIG:Release>:$<$<COMPILE_LANGUAGE:C>:>>")
add_library(control STATIC control.cpp)
target_link_libraries(control PRIVATE dependency)
target_compile_options(control PRIVATE "$<$<BOOL:0>:-DUNUSED>" "$<$<BOOL:0>:-DOTHER>")
target_compile_definitions(control PRIVATE PRESENT=1)
target_include_directories(control PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
include("''' + str(ROOT / 'cmake/KthCompileTests.cmake') + '''")
kth_compile_test_options(control "${CMAKE_CURRENT_BINARY_DIR}/options.rsp")
''')
            configured = subprocess.run([cmake, '-S', str(root), '-B', str(root / 'build'),
                '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=' + compiler], capture_output=True, text=True)
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
            built = subprocess.run([cmake, '--build', str(root / 'build')], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            syntax = ['/Zs'] if Path(compiler).stem.lower() in ('cl', 'clang-cl') else ['-fsyntax-only']
            checked = subprocess.run([compiler, '@' + str(root / 'build/options.rsp'),
                *syntax, str(root / 'control.cpp')], capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)

    def test_literal_response_files_reproduce_target_flags(self):
        cmake = os.environ.get('CMAKE_COMMAND') or shutil.which('cmake')
        self.assertIsNotNone(cmake, 'CMake is required for the compiler-context regression')
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        self.assertIsNotNone(compiler)
        for module, target, variable in [
            ('infrastructure', 'kth_infrastructure_test', 'kth_literal_options'),
            ('domain', 'kth_domain_test', 'kth_address_literal_options'),
        ]:
            with self.subTest(module=module), tempfile.TemporaryDirectory(prefix='compile-context-') as temp:
                root = Path(temp)
                # Use the response-file configuration from the real module.
                text = (ROOT / 'src' / module / 'CMakeLists.txt').read_text()
                start = text.index('    set(' + variable)
                end = text.index('    add_test(', start)
                registration = text[start:end]
                registration = registration.replace('${CMAKE_CURRENT_LIST_DIR}/../../cmake/', str(ROOT / 'cmake') + '/')
                source = '''#include <algorithm>
#ifndef CONFIGURED_FLAG
#error missing configured compiler flags
#endif
#ifndef TARGET_OPTION
#error missing target compile options
#endif
#ifndef TARGET_DEFINE
#error missing target compile definitions
#endif
#if LANGUAGE_OPTION != 23
#error missing evaluated language compile options
#endif
int control() { return std::min(CONFIGURED_FLAG, TARGET_OPTION); }
'''
                (root / 'control.cpp').write_text(source)
                (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(compile_context LANGUAGES CXX)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCONFIGURED_FLAG=17")
add_library(''' + target + ''' STATIC control.cpp)
target_compile_options(''' + target + ''' PRIVATE -DTARGET_OPTION=42)
target_compile_options(''' + target + ''' PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-DLANGUAGE_OPTION=23>")
target_compile_definitions(''' + target + ''' PRIVATE TARGET_DEFINE=1)
target_include_directories(''' + target + ''' PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
''' + registration)
                command = [cmake, '-S', str(root), '-B', str(root / 'build'), '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=' + compiler]
                if sys.platform == 'darwin':
                    sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
                    command.append('-DCMAKE_OSX_SYSROOT=' + sdk)
                configured = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
                built = subprocess.run([cmake, '--build', str(root / 'build'), '--config', 'Release'], capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                rsp = next((root / 'build').glob('*options_Release.rsp'))
                options = ['/std:c++latest', '/Zs'] if Path(compiler).stem.lower() in ('cl', 'clang-cl') else ['-std=c++23', '-fsyntax-only']
                checked = subprocess.run([compiler, '@' + str(rsp), *options, str(root / 'control.cpp')], capture_output=True, text=True)
                self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)


if __name__ == '__main__':
    unittest.main()
