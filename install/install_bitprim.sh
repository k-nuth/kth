#!/bin/bash
#Determine OS
if [ -f /etc/redhat-release ]; then
INSTALL_CMD="sudo yum install -y"
elif [ -f /etc/lsb-release ]; then
INSTALL_CMD="sudo apt-get install -y"
fi

which cmake || $INSTALL_CMD cmake
which pip || sudo url -s "https://bootstrap.pypa.io/get-pip.py"  | python
pip install conan
conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
wget -O conanfile.txt https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt
conan install .
