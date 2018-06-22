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

from argparse import ArgumentParser
from os.path import expanduser

def parse_args():

    parser = ArgumentParser('Bitprim Release Manager')
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

