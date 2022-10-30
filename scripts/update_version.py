#!/usr/bin/env python3

# Copyright (c) 2016-2022 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Usage: python3 update_version.py 0.10.0 --root_path ~/dev/
#        new version is calculated from old version incrementing the minor part

import fnmatch
import os
import fileinput
import argument_parser

projects = ['core', 'consensus', 'database', 'network', 'blockchain', 'node', 'rpc', 'node-cint', 'node-exe']

def get_files(kth_project, f):
    #print(kth_project)
    matches = []
    for root, dirnames, filenames in os.walk(kth_project):
        for filename in fnmatch.filter(filenames, f):
            matches.append(os.path.join(root, filename))
    return matches

def find_and_process_file(kth_project, filename, old_str, new_str):
    files = get_files(kth_project, filename)
    for f in files:
        print(f)
        with fileinput.FileInput(f, inplace=True) as file:
            for line in file:
                print(line.replace(old_str, new_str), end='')

def find_and_process_file_version(kth_project, filename, old_str, new_str, oldmajor, oldminor, oldpatch, newmajor, newminor, newpatch):
    major_str_old = '_MAJOR_VERSION %s' % (oldmajor,)
    minor_str_old = '_MINOR_VERSION %s' % (oldminor,)
    patch_str_old = '_PATCH_VERSION %s' % (oldpatch,)
    major_str_new = '_MAJOR_VERSION %s' % (newmajor,)
    minor_str_new = '_MINOR_VERSION %s' % (newminor,)
    patch_str_new = '_PATCH_VERSION %s' % (newpatch,)


    # print(filename)

    files = get_files(kth_project, filename)

    for f in files:
        # print(f)
        with fileinput.FileInput(f, inplace=True) as file:
            for line in file:
                line = line.replace(old_str, new_str)
                line = line.replace(major_str_old, major_str_new)
                line = line.replace(minor_str_old, minor_str_new)
                print(line.replace(patch_str_old, patch_str_new), end='')



def update_version(root_path, project, oldmajor, oldminor, oldpatch, newmajor, newminor, newpatch):



    kth_project = 'kth-%s' % (project,)
    #path = os.path.join(root_path, kth_project)

    print ('Updating ' + kth_project)

    dep_files = ['kth-%sConfig.cmake.in']
    nodep_files = ['CMakeLists.txt', 'conan_version', 'conanfile.py']
    version_files = ['version.hpp']

    old_str = '%s.%s.%s' % (oldmajor, oldminor, oldpatch)
    new_str = '%s.%s.%s' % (newmajor, newminor, newpatch)

    for nodep_file in nodep_files:
        find_and_process_file(kth_project, nodep_file, old_str, new_str)

    for dep_file in dep_files:
        dep_file = dep_file % (project,)
        find_and_process_file(kth_project, dep_file, old_str, new_str)

    for version_file in version_files:
        find_and_process_file_version(kth_project, version_file, old_str, new_str, oldmajor, oldminor, oldpatch, newmajor, newminor, newpatch)

    return

def main():

    ret, root_path, old_version, new_version, token = argument_parser.parse_args()

    if ret == False:
        return

    for project in projects:
        os.chdir(root_path)
        update_version(root_path, project, old_version[0], old_version[1], old_version[2], new_version[0], new_version[1], new_version[2])

if __name__ == "__main__":
    main()
