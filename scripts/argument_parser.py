#!/usr/bin/env python3

# Copyright (c) 2016-2022 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from argparse import ArgumentParser
from os.path import expanduser

def parse_args():

    parser = ArgumentParser('kth Release Manager')
    parser.add_argument("-rp", "--root_path", dest="root_path", help="root path where the projects are", default=expanduser("~"))
    parser.add_argument('old_version', type=str, nargs=1, help='old version')
    parser.add_argument('new_version', type=str, nargs='?', help='new version')
    parser.add_argument("-t", "--token", dest="token", help="GitHub token", default='')

    args = parser.parse_args()

    old_version = args.old_version[0].split('.')
    if len(old_version) != 3:
        print('old_version has to be of the following format: xx.xx.xx')
        return False,'','','',''

    if args.new_version is None:
        new_version = [old_version[0], str(int(old_version[1]) + 1), old_version[2]]
    else:
        new_version = args.new_version.split('.')
        if len(new_version) != 3:
            print('new_version has to be of the following format: xx.xx.xx')
            return False,'','','',''

    print (new_version)
    print (old_version)

    return True, args.root_path, old_version, new_version, args.token

