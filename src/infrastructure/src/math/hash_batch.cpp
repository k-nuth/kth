// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/hash_batch.hpp>

#include "hash_batch_backends.hpp"

#if defined(ENABLE_X86_SHANI) && (defined(__x86_64__) || defined(__i386__)) \
    && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#endif

namespace kth {

namespace detail {

void sha256d_batch_one_by_one(std::span<byte_span const> messages, std::span<hash_digest> out) {
    for (size_t i = 0; i < messages.size(); ++i) {
        out[i] = bitcoin_hash(messages[i]);
    }
}

bool sha256d_batch_x86_shani_usable() {
#if defined(ENABLE_X86_SHANI) && defined(__SHA__) && defined(__SSE4_1__)
    // The target has the instructions: the binary needs them to run at all.
    return true;
#elif defined(ENABLE_X86_SHANI) && (defined(__x86_64__) || defined(__i386__)) \
    && (defined(__GNUC__) || defined(__clang__))
    // The SHA bit is read from CPUID itself (leaf 7, subleaf 0, EBX bit 29):
    // not every compiler this builds with knows "sha" as a feature name.
    static bool const usable = [] {
        unsigned int eax = 0;
        unsigned int ebx = 0;
        unsigned int ecx = 0;
        unsigned int edx = 0;
        auto const sha = __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) != 0 && (ebx & (1u << 29)) != 0;
        return sha && __builtin_cpu_supports("sse4.1") && __builtin_cpu_supports("ssse3");
    }();
    return usable;
#else
    return false;
#endif
}

} // namespace detail

void bitcoin_hash_batch(std::span<byte_span const> messages, std::span<hash_digest> out) {
#if defined(ENABLE_X86_SHANI)
    if (detail::sha256d_batch_x86_shani_usable()) {
        detail::sha256d_batch_x86_shani(messages, out);
        return;
    }
#endif
    detail::sha256d_batch_one_by_one(messages, out);
}

std::string_view bitcoin_hash_batch_implementation() {
#if defined(ENABLE_X86_SHANI)
    if (detail::sha256d_batch_x86_shani_usable()) {
        return "x86_shani(4 messages in step)";
    }
#endif
    return "one message at a time";
}

} // namespace kth
