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

# Usage:    python3 release.py x.y.z --root_path ~/dev/
#           where x.y.z is the old version
#           new version is calculated from old version incrementing the minor part
#   or
#           python3 release.py x.y.z xx.yy.zz --root_path ~/dev/
#           where x.y.z is the old version and xx.yy.zz is the new version

from argparse import ArgumentParser
from os.path import expanduser
import os
import update_version

projects = ['core', 'consensus', 'database', 'network', 'blockchain', 'node', 'rpc', 'node-cint', 'node-exe']

def commit(new_version):
    os.system('git add .')
    os.system('git commit -m "Release {0}"'.format(new_version))
    os.system('git push --set-upstream origin release-%s' % (new_version,))
    return

def call_update_version(root_path, project,old_version,new_version):
    update_version.update_version(root_path, project, old_version[0], old_version[1], old_version[2], new_version[0], new_version[1], new_version[2])
    return 

def clone():
    os.system('git clone https://github.com/bitprim/bitprim -b dev --recursive')
    os.chdir('bitprim')
    os.system('git submodule update --remote')
    return

def create_branch(new_version):
    os.system('git checkout dev')
    os.system('git pull')
    os.system('git checkout -b release-%s' % (new_version,))
    return

def release(root_path,old_version,new_version):

    os.chdir(root_path)

    if os.path.exists('bitprim') == False:
        print('Cloning...')
        clone()
    else:
        os.chdir('bitprim')

    new_version_str = '.'.join(str(x) for x in new_version)

    for project in projects:
        bitprim_project = 'bitprim-%s' % (project,)
        os.chdir(bitprim_project)
        print ('Creating release branch for ' + bitprim_project)
        create_branch(new_version_str)
        os.chdir(root_path + 'bitprim')
        print ('Updating version number')
        call_update_version(root_path,project,old_version,new_version)
        os.chdir(bitprim_project)
        print ('Commiting release branch for ' + bitprim_project)
        commit(new_version_str)

    return

def main():

    ret, root_path, old_version, new_version = update_version.parse_args()

    if ret == False:
        return

    release(root_path,old_version,new_version)

if __name__ == "__main__":
    main()