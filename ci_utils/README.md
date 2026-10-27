# knuth ci-utils

Set of functions used in Knuth's continuous integration.

# Requirements

* For the jenkins_utils (debian)
```
apt-get update
apt-get install build-essential -y
apt-get install gcc -y
apt-get install git -y
apt-get install cmake -y
apt-get install python -y
apt-get install python-pip -y
pip install conan --upgrade
pip install conan-package-tools
conan config install https://github.com/k-nuth/ci-utils/raw/master/conan/config2023.zip
```

# Usage
* jenkins_dependencies.py

```
python jenkins_dependencies.py -rp="$HOME/devel/testing_script/node" -p="node"
```

## Emscripten toolchain

The local WASM scripts and CI share `ci_utils/conan/profiles/ems6`, using
Emscripten 6.0.13. Use Conan 2.31.0 or newer so SDK 6 gets the modern `-m64`
flag when building wasm64.

The SDK recipe is maintained in `conan-io/conan-toolchains`, rather than the
legacy Conan Center recipe. Set up its local recipe remote once:

```sh
git clone https://github.com/conan-io/conan-toolchains.git ~/.local/share/conan-toolchains
conan remote add conan-toolchains ~/.local/share/conan-toolchains
conan profile show -pr:h ./ci_utils/conan/profiles/ems6
```

Run `scripts/build-ems.sh <version>` or `scripts/build-create-ems.sh <version>`
from the repository root. They use the checked-in profile directly; no named
`ems5` profile is needed. Conan downloads the SDK when building missing tool
packages. The host/build profile remains the normal native profile.
