<img width="200px" src="https://raw.githubusercontent.com/k-nuth/misc/master/images/KTH-and-C.svg" />

# C API <a target="_blank" href="https://github.com/k-nuth/kth-mono/releases">![Github Releases][badge.release]</a> <a target="_blank" href="https://github.com/k-nuth/kth-mono/actions">![Build status][badge.GithubActions]</a> <a href="#">![C][badge.c]</a> <a target="_blank" href="https://t.me/knuth_cash">![Telegram][badge.telegram]</a>

> Bitcoin full node as a C Programming Language library

Knuth is a high performance implementation of the Bitcoin protocol focused on users requiring extra performance and flexibility, what makes it the best platform for wallets, exchanges, block explorers and miners.

## Getting started

Use Conan 2, CMake 3.23 or newer, and a C23-capable compiler.
Building Knuth from source also requires a supported C++23 compiler (GCC 16.2
or newer when using GCC).
Install the build helper and configure Conan:

```sh
pip install kthbuild --user --upgrade
conan config install https://github.com/k-nuth/ci-utils/raw/master/conan/config2023.zip
conan profile detect --exist-ok
```

The C API is included in the unified `kth` package:

```sh
conan install --requires=kth/1.7.0 -o 'kth/*:shared=True' --build=missing
```

### "Hello, Knuth!"

Save the following as `hello_knuth.c`:

```c
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <kth/capi.h>

int main(int argc, char** argv) {
    if (argc != 2 && (argc != 3 || strcmp(argv[2], "--init") != 0)) {
        fprintf(stderr, "Usage: %s CONFIG [--init]\n", argv[0]);
        return 1;
    }

    kth_settings* settings = NULL;
    char* error = NULL;
    if (!kth_config_settings_get_from_file(argv[1], &settings, &error)) {
        fprintf(stderr, "Configuration: %s\n", error ? error : "unknown error");
        kth_core_destruct_string(error);
        return 1;
    }

    kth_node_t node = kth_node_construct(settings, 1);
    kth_config_settings_destruct(settings);
    if (node == NULL) {
        fprintf(stderr, "Could not construct the node\n");
        return 1;
    }

    int result = 1;
    if (argc == 3 && !kth_node_initchain(node)) {
        fprintf(stderr, "Could not initialize the database\n");
        goto cleanup;
    }

    kth_error_code_t ec = kth_node_init_run_sync(node, kth_start_modules_all);
    if (ec != kth_ec_success) {
        fprintf(stderr, "Could not start the node: %d\n", (int)ec);
        goto cleanup;
    }

    // The chain handle is borrowed and is valid only while the node is running.
    kth_chain_t chain = kth_node_get_chain(node);
    kth_size_t height = 0;
    if (chain != NULL && kth_chain_sync_last_height(chain, &height) == kth_ec_success) {
        printf("%" PRIu64 "\n", (uint64_t)height);
        result = 0;
    } else {
        fprintf(stderr, "Could not read the chain height\n");
    }

cleanup:
    kth_node_close(node);
    kth_node_destruct(node);
    return result;
}
```

### How it works

`kth_config_settings_get_from_file` loads the configuration into an owned
`kth_settings` object. Pass an existing Knuth configuration file as the first
argument. The constructor copies these settings, so the example releases them
with `kth_config_settings_destruct` immediately afterward. The constructor's
second argument enables stdout logging; it is a boolean, not a `FILE*`.

Use `--init` only on the first run, to create the blockchain database at the
configured location. Initialization failures and startup errors are reported
before attempting a query.

`kth_node_init_run_sync` waits for startup to finish, then returns while the node
is running. Only then does the example obtain the borrowed chain handle, read
the current height, and print it. This is a short query example: it closes and
destroys the node immediately afterward; it does not wait for the initial block
download to finish. For a long-running node, use
`kth_node_init_run_and_wait_for_signal` and perform queries after successful
startup, before shutdown.

### Build and run

Create `conanfile.txt` with the shared-library requirement and Conan 2 generators:

```sh
printf "[requires]\nkth/1.7.0\n[options]\nkth/*:shared=True\n[generators]\nCMakeDeps\nCMakeToolchain\n" > conanfile.txt
```

Save this as `CMakeLists.txt` alongside `hello_knuth.c`:

```cmake
cmake_minimum_required(VERSION 3.23)
project(hello_knuth LANGUAGES C CXX)
find_package(kth CONFIG REQUIRED)
add_executable(hello_knuth hello_knuth.c)
set_target_properties(hello_knuth PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED YES)
target_link_libraries(hello_knuth PRIVATE kth::c-api)
```

The imported `kth::c-api` target supplies the actual `c-api` library, include
paths, compile definitions, and transitive dependencies. There is no
`kth-c-api` library to pass to the linker.

Install dependencies and build a Release executable:

```sh
conan install . --output-folder=build -s build_type=Release --build=missing
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On Linux and macOS, activate Conan's generated runtime environment so the shared
libraries can be found, then run the example with your configuration:

```sh
. build/conanrun.sh
./build/hello_knuth /path/to/kth.cfg --init
# Subsequent runs use the existing database:
./build/hello_knuth /path/to/kth.cfg
```

On Windows, use `build\conanrun.bat` and `build\Release\hello_knuth.exe` with a
multi-configuration generator.

For more more detailed instructions, please refer to our [documentation](https://kth.cash/docs/).

## Issues

Each of our modules has its own Github repository, but in case you want to create an issue, please do so in our [main repository](https://github.com/k-nuth/kth-mono/issues).


<!-- Links -->
[badge.Cirrus]: https://api.cirrus-ci.com/github/k-nuth/kth-mono.svg?branch=master
[badge.GithubActions]: https://img.shields.io/endpoint.svg?url=https%3A%2F%2Factions-badge.atrox.dev%2Fk-nuth%2Fkth-mono%2Fbadge&style=for-the-badge
[badge.version]: https://badge.fury.io/gh/k-nuth%2Fkth-mono.svg
[badge.release]: https://img.shields.io/github/v/release/k-nuth/kth-mono?display_name=tag&style=for-the-badge&color=A8B9CC&logo=c
[badge.c]: https://img.shields.io/badge/C-23-blue.svg?logo=c&style=for-the-badge
[badge.telegram]: https://img.shields.io/badge/telegram-badge-blue.svg?logo=telegram&style=for-the-badge

<!-- [badge.Gitter]: https://img.shields.io/badge/gitter-join%20chat-blue.svg -->
