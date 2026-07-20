#!/usr/bin/env python

#
# Copyright (c) 2016-present Knuth Project developers.
#

import os

def get_version_from_file():
    return get_content_default('conan_version')

def get_version():
    version = get_version_from_file()

    if version is None:
        version = os.getenv("KTH_CONAN_VERSION", None)

    if version is None:
        version = get_version_from_branch_name()

    if version is None:
        version = get_version_from_git_describe(None, is_development_branch())

    return version


def get_version_from_branch_name():
    branch = get_branch()

    # print("get_version_from_branch_name - branch: %s" % (branch,))

    if branch is None:
        return None

    if branch.startswith("release-") or branch.startswith("hotfix-"):
        return branch.split('-', 1)[1]

    if branch.startswith("release_") or branch.startswith("hotfix_"):
        return branch.split('_', 1)[1]

    return None


def get_version_from_git_describe_no_releases(default=None, is_dev_branch=False):
    describe = get_git_describe()

    # print('describe')
    # print(describe)

    if describe is None:
        return None
    version = describe.split('-')[0][1:]

    if is_dev_branch:
        version_arr = version.split('.')
        if len(version_arr) != 3:
            # print('version has to be of the following format: xx.xx.xx')
            return None
        # version = "%s.%s.%s" % (version_arr[0], str(int(version_arr[1]) + 1), version_arr[2])
        version = "%s.%s.%s" % (version_arr[0], str(int(version_arr[1]) + 1), 0)

    return version

def get_branch():
    branch = os.getenv("KTH_BRANCH", None)

    # print("branch: %s" % (branch,))

    if branch is None:
        branch = get_git_branch()

    # print("branch: %s" % (branch,))

    return branch


def get_git_describe(default=None):
    try:
        res = subprocess.Popen(["git", "describe", "master"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        output, _ = res.communicate()
        if output:
            if res.returncode == 0:
                return output.decode("utf-8").replace('\n', '').replace('\r', '')
                # return output.replace('\n', '').replace('\r', '')
        return default
    except OSError: # as e:
        return default
    except:
        return default

def get_version_from_git_describe(default=None, is_dev_branch=False):
    describe = get_git_describe()

    # print('describe')
    # print(describe)

    # if describe is None:
    #     return None

    if describe is None:
        describe = "v0.0.0-"

    version = describe.split('-')[0][1:]

    if is_dev_branch:
        # print(version)
        # print(release_branch_version_to_int(version))

        # print(max_release_branch())

        max_release_i, max_release_s = max_release_branch()

        if max_release_i is not None and max_release_i > release_branch_version_to_int(version):
            version = max_release_s

        version_arr = version.split('.')
        if len(version_arr) != 3:
            # print('version has to be of the following format: xx.xx.xx')
            return None
        # version = "%s.%s.%s" % (version_arr[0], str(int(version_arr[1]) + 1), version_arr[2])
        version = "%s.%s.%s" % (version_arr[0], str(int(version_arr[1]) + 1), 0)

    return version

def get_content_default(file_name, default=None):
    try:
        return get_content(file_name)
    except IOError:
        return default

def get_content(file_name):
    file_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', file_name)
    return access_file(file_path)

def access_file(file_path):
    with open(file_path, 'r') as f:
        return f.read().replace('\n', '').replace('\r', '')

def get_git_branch(default=None):
    try:
        res = subprocess.Popen(["git", "rev-parse", "--abbrev-ref", "HEAD"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        output, _ = res.communicate()
        # print('fer 0')

        if output:
            # print('fer 0.1')
            if res.returncode == 0:
                # print('fer 0.2')
                # print(output)
                # print(output.decode("utf-8"))
                # print(output.decode("utf-8").replace('\n', ''))
                ret = output.decode("utf-8").replace('\n', '').replace('\r', '')
                # print(ret)
                return ret
        return default
    except OSError: # as e:
        # print('fer 1')
        return default
    except:
        # print('fer 2')
        return default

def is_development_branch():
    branch = get_branch()
    if branch is None:
        return False

    # return branch == 'dev' or branch.startswith('feature')

    if branch == 'master':
        return False
    if branch.startswith('release'):
        return False
    if branch.startswith('hotfix'):
        return False

    return True


def branch_to_channel(branch):
    if branch is None:
        return "staging"
    if branch == 'dev':
        return "testing"
    if branch.startswith('release'):
        return "staging"
    if branch.startswith('hotfix'):
        return "staging"
    if branch.startswith('feature'):
        return branch

    return "staging"

def get_channel_from_file():
    return get_content_default('conan_channel')

def get_channel_from_branch():
    return branch_to_channel(get_branch())

def get_channel():
    channel = get_channel_from_file()

    if channel is None:
        channel = os.getenv("KTH_CONAN_CHANNEL", None)

    if channel is None:
        # channel = get_git_branch()
        channel = get_channel_from_branch()

    if channel is None:
        channel = 'staging'

    return channel
# def main():
#     print(get_version())

# if __name__ == "__main__":
#     main()