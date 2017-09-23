mkdir build
cd build
conan install ..
cmake -G "Visual Studio 15 2017 Win64" -DWITH_TESTS=OFF  ..

REM Open Visual Studio .sln file and Build the solution