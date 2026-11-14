// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <crypto/sha256.h>
#include <array>
#include <cstdio>

namespace {
void print(unsigned char const* bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) std::printf("%02x", bytes[i]);
    std::puts("");
}
}

int main() {
    auto const selected = kth::SHA256Implementation();
    if (selected != kth::SHA256AutoDetect()) return 1;
#ifdef KTH_SHA256_COMPILE_TIME
    // The legacy startup call must not change a fixed build's implementation.
    if (kth::SHA256AutoDetect(kth::sha256_implementation::STANDARD) != selected) return 2;
#endif
    std::printf("implementation=%s\n", selected.c_str());
    std::array<unsigned char, 64 * 17 + 1> input{};
    for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<unsigned char>(i * 37 + 11);
    std::array<unsigned char, 32 * 17> output{};
    kth::CSHA256{}.Write(input.data() + 1, 13).Write(input.data() + 14, input.size() - 14).Finalize(output.data());
    print(output.data(), 32);
    for (size_t count : {0, 1, 2, 3, 4, 7, 8, 9, 17}) {
        kth::SHA256D64(output.data(), input.data() + 1, count);
        print(output.data(), count * 32);
        // SHA256D64 also supports compacting in place.
        auto in_place = input;
        kth::SHA256D64(in_place.data() + 1, in_place.data() + 1, count);
        print(in_place.data() + 1, count * 32);
    }
}
