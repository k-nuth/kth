// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_64.hpp>

#ifndef __EMSCRIPTEN__
#include <simdutf.h>
#elif defined(__wasm_simd128__) && !defined(KTH_DISABLE_WASM_SIMD)
#include <kth/infrastructure/impl/formats/base_64_wasm.hpp>
#endif

namespace kth::detail {

std::string encode_base64_runtime(byte_span input) {
#ifndef __EMSCRIPTEN__
    if (input.empty())
        return {};
    std::string out;
    out.resize_and_overwrite(simdutf::base64_length_from_binary(input.size()), [input](char *bytes, size_t) noexcept {
        return simdutf::binary_to_base64(reinterpret_cast<char const *>(input.data()), input.size(), bytes);
    });
    return out;
#elif defined(__wasm_simd128__) && !defined(KTH_DISABLE_WASM_SIMD)
    std::string out;
    auto const size = (input.size() / 3 + size_t(input.size() % 3 != 0)) * 4;
    out.resize_and_overwrite(size, [input, size](char *bytes, size_t) noexcept {
        auto output = std::span(bytes, size);
        auto const consumed = detail::base64_wasm_encode_prefix(input, output);
        detail::base64_encode_into(input.subspan(consumed), output.subspan((consumed / 3) * 4));
        return size;
    });
    return out;
#else
    std::string out;
    auto const size = (input.size() / 3 + size_t(input.size() % 3 != 0)) * 4;
    out.resize_and_overwrite(size, [input, size](char *bytes, size_t) noexcept {
        detail::base64_encode_into(input, std::span(bytes, size));
        return size;
    });
    return out;
#endif
}

std::expected<size_t, base64_errc> decode_base64_runtime(std::string_view input, std::span<uint8_t> output) noexcept {
#ifndef __EMSCRIPTEN__
    if (input.empty())
        return size_t(0);
    // simdutf's safe API accepts exact-sized buffers and never writes past them.
    size_t size = output.size();
    char empty = 0;
    auto const result = simdutf::base64_to_binary_safe(
        input.data(), input.size(), output.empty() ? &empty : reinterpret_cast<char *>(output.data()), size);
    if (result.error == simdutf::error_code::OUTPUT_BUFFER_TOO_SMALL)
        return std::unexpected(base64_errc::output_too_small);
    if (result.error == simdutf::error_code::BASE64_INPUT_REMAINDER)
        return std::unexpected(base64_errc::invalid_length);
    if (result.error != simdutf::error_code::SUCCESS) {
        // Only invalid input takes this scalar classification path; valid data
        // stays on the SIMD fast path.
        auto const scalar = decode_base64_scalar(input, output);
        if (!scalar)
            return std::unexpected(scalar.error());
        return std::unexpected(base64_errc::invalid_character);
    }
    return size;
#else
    size_t consumed = 0;
#if defined(__wasm_simd128__) && !defined(KTH_DISABLE_WASM_SIMD)
    consumed = detail::base64_wasm_decode_prefix(input, output);
#endif
    auto const written = (consumed / 4) * 3;
    auto const result = decode_base64_scalar(input.substr(consumed), output.subspan(written));
    if (!result)
        return std::unexpected(result.error());
    return written + *result;
#endif
}

std::expected<data_chunk, base64_errc> decode_base64_runtime(std::string_view input) {
#ifndef __EMSCRIPTEN__
    if (input.size() < 4)
        return decode_base64_scalar(input);
    // Owning buffers use simdutf's fastest API with its documented upper bound.
    // The caller-provided span overload above must instead use the safe API.
    auto const capacity = simdutf::maximal_binary_length_from_base64(input.data(), input.size());
    data_chunk out(std::max(size_t(1), capacity));
    auto const result = simdutf::base64_to_binary(input.data(), input.size(), reinterpret_cast<char *>(out.data()));
    if (result.error != simdutf::error_code::SUCCESS) {
        auto const scalar = decode_base64_scalar(input, out);
        if (!scalar)
            return std::unexpected(scalar.error());
        return std::unexpected(base64_errc::invalid_character);
    }
    out.erase(out.begin() + result.count, out.end());
#else
    data_chunk out(detail::base64_capacity(input.size()));
    auto const result = decode_base64_runtime(input, out);
    if (!result)
        return std::unexpected(result.error());
    out.erase(out.begin() + *result, out.end());
#endif
    return out;
}
} // namespace kth::detail
