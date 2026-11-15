# Concatenates the kernel core and the Metal entry point into a C++ raw
# string literal, so the Metal runtime can compile the kernel from source.
file(READ "${CORE}" core)
file(READ "${ENTRY}" entry)
file(WRITE "${OUT}" "R\"KTHMSL(#include <metal_stdlib>\nusing namespace metal;\n${core}\n${entry}\n)KTHMSL\"\n")
