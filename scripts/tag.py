#!/usr/bin/env python3

# Copyright (c) 2016-2022 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
    os.chdir('kth')

    new_version_str = '.'.join(str(x) for x in new_version)

    for project in projects:
        kth_project = 'kth-%s' % (project,)
        os.chdir(kth_project)
        print ('Creating tag for ' + kth_project)
        make_tag(new_version_str)
        os.chdir(root_path + 'kth')

    return


def main():

    ret, root_path, old_version, new_version, token = argument_parser.parse_args()

    if ret == False:
        return

    tag_repos(root_path,new_version)

if __name__ == "__main__":
    main()