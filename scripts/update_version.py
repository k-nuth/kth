#!/usr/bin/env python3

#
# Copyright (c) 2017-2018 Bitprim Inc.
#
# This file is part of Bitprim.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# Usage: python3 update_version.py 0.10.0 --root_path ~/dev/
#        new version is calculated from old version incrementing the minor part

import fnmatch
import os
import fileinput
import glob
from argparse import ArgumentParser
from os.path import expanduser

projects = ['core', 'consensus', 'database', 'network', 'blockchain', 'node', 'rpc', 'node-cint', 'node-exe']

def get_files(bitprim_project, f):
    print(bitprim_project)
    matches = []
    for root, dirnames, filenames in os.walk(bitprim_project):
        for filename in fnmatch.filter(filenames, f):
            matches.append(os.path.join(root, filename))
    return matches

def find_and_process_file(bitprim_project, filename, old_str, new_str):
    files = get_files(bitprim_project, filename)
    for f in files:
        # print(f)
        with fileinput.FileInput(f, inplace=True) as file:
            for line in file:
                print(line.replace(old_str, new_str), end='')

def find_and_process_file_version(bitprim_project, filename, old_str, new_str, oldmajor, oldminor, oldpatch, newmajor, newminor, newpatch):
    major_str_old = '_MAJOR_VERSION %s' % (oldmajor,)
    minor_str_old = '_MINOR_VERSION %s' % (oldminor,)
    patch_str_old = '_PATCH_VERSION %s' % (oldpatch,)
    major_str_new = '_MAJOR_VERSION %s' % (newmajor,)
    minor_str_new = '_MINOR_VERSION %s' % (newminor,)
    patch_str_new = '_PATCH_VERSION %s' % (newpatch,)


    # print(filename)

    files = get_files(bitprim_project, filename)

    for f in files:
        # print(f)
        with fileinput.FileInput(f, inplace=True) as file:
            for line in file:
                line = line.replace(old_str, new_str)
                line = line.replace(major_str_old, major_str_new)
                line = line.replace(minor_str_old, minor_str_new)
                print(line.replace(patch_str_old, patch_str_new), end='')



def update_version(root_path, project, oldmajor, oldminor, oldpatch, newmajor, newminor, newpatch):
    bitprim_project = 'bitprim-%s' % (project,)
    path = os.path.join(root_path, bitprim_project)
    dep_files = ['bitprim-%sConfig.cmake.in']
    nodep_files = ['CMakeLists.txt', 'conan_version', 'conanfile.py']
    version_files = ['version.hpp']

    old_str = '%s.%s.%s' % (oldmajor, oldminor, oldpatch)
    new_str = '%s.%s.%s' % (newmajor, newminor, newpatch)

    for nodep_file in nodep_files:
        find_and_process_file(bitprim_project, nodep_file, old_str, new_str)

    for dep_file in dep_files:
        dep_file = dep_file % (project,)
        find_and_process_file(bitprim_project, dep_file, old_str, new_str)

    for version_file in version_files:
        find_and_process_file_version(bitprim_project, version_file, old_str, new_str, oldmajor, oldminor, oldpatch, newmajor, newminor, newpatch)

    return

def parse_args():

    parser = ArgumentParser('Bitprim version updater.')
    parser.add_argument("-rp", "--root_path", dest="root_path", help="root path where the projects are", default=expanduser("~"))
    parser.add_argument('old_version', type=str, nargs=1, help='old version')
    parser.add_argument('new_version', type=str, nargs='?', help='new version')
    args = parser.parse_args()

    old_version = args.old_version[0].split('.')
    if len(old_version) != 3:
        print('old_version has to be of the following format: xx.xx.xx')
        return False,'','',''

    if args.new_version is None:
        new_version = [old_version[0], str(int(old_version[1]) + 1), old_version[2]]
    else:
        new_version = args.new_version.split('.')
        if len(new_version) != 3:
            print('new_version has to be of the following format: xx.xx.xx')
            return False,'','',''

    print (new_version)
    print (old_version)

    return True, args.root_path, old_version, new_version


def main():

    ret, root_path, old_version, new_version = parse_args()

    if ret == False:
        return

    for project in projects:
        update_version(root_path, project, old_version[0], old_version[1], old_version[2], new_version[0], new_version[1], new_version[2])

if __name__ == "__main__":
    main()
