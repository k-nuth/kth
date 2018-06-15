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

projects = ['core', 'consensus', 'database', 'network', 'blockchain', 'node', 'rpc', 'node-cint', 'node-exe']

def commit(new_version):
    os.system('git add .')
    os.system('git commit -m "Release {0}"'.format(new_version))
    os.system('git push --set-upstream origin release-%s' % (new_version,))
    return

def update_version(old_version, new_version, root_path):
    program = '{0}bitprim/scripts/update_version.py {1} {2} --root_path {3}'.format(root_path,old_version, new_version,root_path + 'bitprim/')
    print(program)
    os.system(program)
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

def release(root_path,oldversion,newversion):

    os.chdir(root_path)

    if os.path.exists('bitprim') == False:
        clone()
    else:
        os.chdir('bitprim')

    old_version = '.'.join(str(x) for x in oldversion)
    new_version = '.'.join(str(x) for x in newversion)

    for project in projects:
        bitprim_project = 'bitprim-%s' % (project,)
        os.chdir(bitprim_project)
        create_branch(new_version)
        os.chdir(root_path + 'bitprim')

    os.chdir(root_path)
    os.chdir('bitprim')
    
    update_version(old_version, new_version,root_path)
    
    for project in projects:
        bitprim_project = 'bitprim-%s' % (project,)
        os.chdir(bitprim_project)
        commit(new_version)
        os.chdir(root_path + 'bitprim')

    return

def main():

    parser = ArgumentParser('Bitprim Release.')
    parser.add_argument("-rp", "--root_path", dest="root_path", help="root path where the projects are", default=expanduser("~"))
    parser.add_argument('old_version', type=str, nargs=1, help='old version')
    parser.add_argument('new_version', type=str, nargs='?', help='new version')
    args = parser.parse_args()

    old_version = args.old_version[0].split('.')
    if len(old_version) != 3:
        print('old_version has to be of the following format: x.y.z')
        return

    if args.new_version is None:
        new_version = [old_version[0], str(int(old_version[1]) + 1), old_version[2]]
    else:
        new_version = args.new_version.split('.')
        if len(new_version) != 3:
            print('new_version has to be of the following format: xx.yy.zz')
            return

    print (new_version)
    print (old_version)

    release(args.root_path,old_version,new_version)

if __name__ == "__main__":
    main()