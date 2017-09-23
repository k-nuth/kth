mkdir build
cd build
conan install ..
cmake -G "Visual Studio 15 2017 Win64" -DWITH_TESTS=OFF  ..

REM Open Visual Studio .sln file and Build the solution

REM ---------------------------------
REM Debug
REM ---------------------------------

conan install bitprim-conan-boost/1.64.0@bitprim/stable -s build_type=Debug
conan install .. -s build_type=Debug
cmake -G "Visual Studio 15 2017 Win64" -DWITH_TESTS=OFF  ..