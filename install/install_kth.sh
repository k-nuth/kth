#!/bin/bash
#Determine OS
if [ -f /etc/redhat-release ]; then
INSTALL_CMD="sudo yum install -y"
elif [ -f /etc/lsb-release ]; then
INSTALL_CMD="sudo apt-get install -y"
fi

which cmake || $INSTALL_CMD cmake
which pip || sudo wget -qO- "https://bootstrap.pypa.io/get-pip.py"  | python
pip install conan
conan remote add kth https://api.bintray.com/conan/k-nuth/kth
wget -O conanfile.txt https://raw.githubusercontent.com/k-nuth/kth/dev/install/conanfile.txt
conan install .
