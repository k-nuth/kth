# Optional features

Some features are not part of the Bitcoin Cash protocol that every node speaks:
a node works on the network without them, and other implementations may or may
not have them. Each of those is built only when asked for.

## The rule

An optional feature has:

- **A Conan option** named after the feature, **off by default**. It sets the CMake
  option `KTH_WITH_<FEATURE>` and the compile definition of the same name.
- **Its sources behind that option.** With the option off, nothing of the
  feature is compiled, and its headers refuse to be included.
- **Its tests in an executable of their own**, built only with the option on.
- **One workflow** that builds with the option on and runs those tests, and no
  others. The main workflows build with every optional feature off.

One option covers a whole feature: what is already in the tree and what later
pull requests add to it.

## Features

| Option | Feature | Tests | Workflow |
|---|---|---|---|
| `neutrino` | Compact block filters for light wallets (Neutrino, BIP157/158): the Golomb-coded set, the regular filter of a block, and the script filter of a block with its entry tree | `kth_domain_neutrino_test` | `neutrino.yml` |

## Building with a feature on

```
conan install conanfile.py --version 0.0.0 -of build --build=missing \
    -o tests=True -o neutrino=True
cmake --preset conan-release -DGLOBAL_BUILD=ON -DENABLE_TEST=ON
cmake --build --preset conan-release --target kth_domain_neutrino_test
```
