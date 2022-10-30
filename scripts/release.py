#!/usr/bin/env python3

# Copyright (c) 2016-2022 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#requeries
# -pygithub
#   pip install pygithub

# Usage:    python3 release.py x.y.z --root_path ~/dev/
#           where x.y.z is the old version
#           new version is calculated from old version incrementing the minor part
#   or
#           python3 release.py x.y.z xx.yy.zz --root_path ~/dev/
#           where x.y.z is the old version and xx.yy.zz is the new version

#from argparse import ArgumentParser
#from os.path import expanduser
import os
import update_version
import argument_parser
from github import Github

projects = ['core', 'consensus', 'database', 'network', 'blockchain', 'node', 'rpc', 'node-cint', 'node-exe']

def create_pr(project,new_version, token):
    g = Github(token)
    org = g.get_organization('kth')
    repo = org.get_repo(project)
    pr = repo.create_pull('Release ' + new_version,'','master','release-' + new_version,True )
    print('PR created:' + str(pr.id))
    return

def commit(new_version):
    os.system('git add .')
    os.system('git commit -m "Release {0}"'.format(new_version))
    os.system('git push --set-upstream origin release-%s' % (new_version,))
    return

def call_update_version(root_path, project,old_version,new_version):
    update_version.update_version(root_path, project, old_version[0], old_version[1], old_version[2], new_version[0], new_version[1], new_version[2])
    return

def clone():
    os.system('git clone https://github.com/k-nuth/kth -b dev --recursive')
    os.chdir('kth')
    os.system('git submodule update --remote')
    return

def create_branch(new_version):
    os.system('git checkout dev')
    os.system('git pull')
    os.system('git checkout -b release-%s' % (new_version,))
    return

def release(root_path,old_version,new_version, token):

    os.chdir(root_path)

    if os.path.exists('kth') == False:
        print('Cloning...')
        clone()
    else:
        os.chdir('kth')

    new_version_str = '.'.join(str(x) for x in new_version)

    for project in projects:
        kth_project = 'kth-%s' % (project,)
        os.chdir(kth_project)
        print ('Creating release branch for ' + kth_project)
        create_branch(new_version_str)
        os.chdir(root_path + 'kth')
        print ('Updating version number')
        call_update_version(root_path, project, old_version, new_version)
        os.chdir(kth_project)
        print ('Commiting release branch for ' + kth_project)
        commit(new_version_str)
        if token != '':
            print ('Creating PR for ' + kth_project)
            create_pr(kth_project, new_version_str, token)
        os.chdir(root_path + 'kth')

    return

def main():

    ret, root_path, old_version, new_version, token = argument_parser.parse_args()

    if ret == False:
        return

    release(root_path, old_version, new_version, token)

if __name__ == "__main__":
    main()