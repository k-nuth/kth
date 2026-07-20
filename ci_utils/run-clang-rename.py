#!/usr/bin/env python

# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
clang-rename for multiple translation units
===========================================

Runs clang-rename over all files in a compilation database. Requires clang-rename in $PATH.

Example invocations.
- Run clang-rename on all files in the current working directory with a default
  set of checks and show warnings in the cpp files and all project headers.
    run-clang-rename.py $PWD

- Fix all header guards.
    run-clang-rename.py -fix -checks=-*,llvm-header-guard

- Fix all header guards included from clang-rename and header guards
  for clang-rename headers.
    run-clang-rename.py -fix -checks=-*,llvm-header-guard extra/clang-rename \
                      -header-filter=extra/clang-rename

Compilation database setup:
http://clang.llvm.org/docs/HowToSetupToolingForLLVM.html
"""

from __future__ import print_function

import argparse
import glob
import json
import multiprocessing
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import traceback
import yaml

is_py2 = sys.version[0] == '2'

def find_compilation_database(path):
  """Adjusts the directory until a compilation database is found."""
  result = './'
  while not os.path.isfile(os.path.join(result, path)):
    if os.path.realpath(result) == '/':
      print('Error: could not find compilation database.')
      sys.exit(1)
    result += '../'
  return os.path.realpath(result)

def make_absolute(f, directory):
  if os.path.isabs(f):
    return f
  return os.path.normpath(os.path.join(directory, f))

def get_rename_invocation(f, clang_rename_binary, build_path, input_path):
  """Gets a command line for clang-rename."""
  start = [clang_rename_binary]
  start.append('-p=' + build_path)
  start.append('-input=' + input_path)
  start.append(f)
  return start

def run_rename(args, build_path, input_path, name, failed_files):
  while True:
    invocation = get_rename_invocation(name, args.clang_rename_binary, build_path, input_path)

    sys.stdout.write(' '.join(invocation) + '\n')
    return_code = subprocess.call(invocation)
    if return_code != 0:
      failed_files.append(name)

def main():
# clang-rename -input=test.yaml test.cpp

  parser = argparse.ArgumentParser(description='Runs clang-rename over all files '
                                   'in a compilation database. Requires '
                                   'clang-rename and clang-apply-replacements in '
                                   '$PATH.')
  parser.add_argument('-clang-rename-binary', metavar='PATH',
                      default='clang-rename',
                      help='path to clang-rename binary')
  parser.add_argument('files', nargs='*', default=['.*'],
                      help='files to be processed (regex on path)')
  parser.add_argument('-p', dest='build_path',
                      help='Path used to read a compile command database.')
  parser.add_argument('-input', dest='input_path',
                      help='Path to the input YAML replacement file.')

  args = parser.parse_args()

  db_path = 'compile_commands.json'
  # if args.build_path is not None:
  #   build_path = args.build_path
  # else:
  #   # Find our database
  #   build_path = find_compilation_database(db_path)

  # input_path = 'replacements.yaml'
  # if args.input_path is not None:
  #   input_path = args.input_path

  build_path = '/Users/fernando/dev/kth/infrastructure/build'
  input_path = '/Users/fernando/dev/kth/infrastructure/replacements.yaml'

  # try:
  #   invocation = [args.clang_rename_binary]
  #   invocation.append('-p=' + build_path)
  #   invocation.append('-input=' + input_path)
  #   # if args.checks:
  #   #   invocation.append('-checks=' + args.checks)
  #   invocation.append('-')
  #   subprocess.check_call(invocation)
  # except:
  #   print("Unable to run clang-rename.", file=sys.stderr)
  #   sys.stderr.write(' '.join(invocation) + '\n')
  #   sys.exit(1)

  # Load the database and extract all files.
  database = json.load(open(os.path.join(build_path, db_path)))
  files = [make_absolute(entry['file'], entry['directory'])
           for entry in database]

  # Build up a big regexy filter from all command line arguments.
  file_name_re = re.compile('|'.join(args.files))

  return_code = 0
  try:
    # List of files with a non-zero return code.
    failed_files = []

    for name in files:
      if file_name_re.search(name):
        run_rename(args, build_path, input_path, name, failed_files)

    if len(failed_files):
      return_code = 1

  except KeyboardInterrupt:
    # This is a sad hack. Unfortunately subprocess goes
    # bonkers with ctrl-c and we start forking merrily.
    print('\nCtrl-C detected, goodbye.')
    os.kill(0, 9)

  sys.exit(return_code)

if __name__ == '__main__':
  main()
