// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PSEUDO_RANDOM_HPP
#define KTH_INFRASTRUCTURE_PSEUDO_RANDOM_HPP

#include <cstdint>
#include <random>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/data.hpp>

// Apple and Emscripten
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
#include <unistd.h>
#endif

#ifndef __EMSCRIPTEN__
#include <sys/random.h>
#endif
namespace kth {

struct KI_API pseudo_random {
    template <typename Container>
        requires std::is_trivially_copyable<typename Container::value_type>::value
    static
    void fill(Container& out) {
        using value_type = typename Container::value_type;

        size_t size_bytes = out.size() * sizeof(value_type);
        if (size_bytes == 0) {
            return;
        }

        uint8_t* buffer_ptr = reinterpret_cast<uint8_t*>(out.data());
        // memset(buffer_ptr, 0x12, size_bytes);
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
        if (getentropy(buffer_ptr, size_bytes) != 0) {
            throw std::runtime_error("getentropy() failed in WASM");
        }
#else
        size_t offset = 0;
        while (offset < size_bytes) {
            ssize_t ret = ::getrandom(buffer_ptr + offset, size_bytes - offset, 0);
            if (ret < 0) {
                throw std::runtime_error("getrandom() failed");
            }
            offset += ret;
        }
#endif
    }

    static void fill(uint8_t* out, size_t size_bytes) {
        if (size_bytes == 0) {
            return;
        }
        // memset(out, 0x12, size_bytes);

#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
        if (getentropy(out, size_bytes) != 0) {
            throw std::runtime_error("getentropy() failed in WASM");
        }
#else
        size_t offset = 0;
        while (offset < size_bytes) {
            ssize_t ret = ::getrandom(out + offset, size_bytes - offset, 0);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                //TODO: replace exceptions with std::expected
                throw std::runtime_error("getrandom() failed, errno=" + std::to_string(errno));
            }
            offset += static_cast<size_t>(ret);
        }
#endif
    }
};


/**
 * Fill a buffer with randomness
 */
KI_API void pseudo_random_fill(data_chunk& out);

KI_API void pseudo_random_fill(uint8_t* out, size_t size);

} // namespace kth

#endif
