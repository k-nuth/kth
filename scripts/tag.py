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

# Usage:    python3 tag.py x.y.z --root_path ~/dev/
#           where x.y.z is the old version
#           new version is calculated from old version incrementing the minor part
#   or
#           python3 tag.py x.y.z xx.yy.zz --root_path ~/dev/
#           where x.y.z is the old version and xx.yy.zz is the new version

import argument_parser
import os

projects = ['core', 'consensus', 'database', 'network', 'blockchain', 'node', 'rpc', 'node-cint', 'node-exe']

def make_tag(new_version):
    os.system('git checkout master')
    os.system('git pull')
    os.system('git tag -a v{0} -m "version {0}"'.format(new_version))
    os.system('git push --tags')
    return

def tag_repos(root_path,new_version):

    os.chdir(root_path)
    os.chdir('bitprim')

    new_version_str = '.'.join(str(x) for x in new_version)

    for project in projects:
        bitprim_project = 'bitprim-%s' % (project,)
        os.chdir(bitprim_project)
        print ('Creating tag for ' + bitprim_project)
        make_tag(new_version_str)
        os.chdir(root_path + 'bitprim')

    return


def main():

    ret, root_path, old_version, new_version, token = argument_parser.parse_args()

    if ret == False:
        return

    tag_repos(root_path,new_version)

if __name__ == "__main__":
    main()