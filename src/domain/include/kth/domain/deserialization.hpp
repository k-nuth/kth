// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_DESERIALIZATION_HPP
#define KTH_DOMAIN_DESERIALIZATION_HPP

// #include <cstddef>
// #include <cstdint>
// #include <expected>

// #include <memory>
// #include <string>
// #include <vector>

#include <expected>


#include <kth/domain/concepts.hpp>
#include <kth/domain/constants/functions.hpp>

#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth {

template <typename T>
using expect = std::expected<T, code>;

// using hash_digest = byte_array<hash_size>;
// using half_hash = byte_array<half_hash_size>;
// using quarter_hash = byte_array<quarter_hash_size>;
// using long_hash = byte_array<long_hash_size>;
// using short_hash = byte_array<short_hash_size>;
// using mini_hash = byte_array<mini_hash_size>;

template <size_t N>
concept is_hash_size = (N == hash_size ||
                        N == half_hash_size ||
                        N == quarter_hash_size ||
                        N == long_hash_size ||
                        N == short_hash_size ||
                        N == mini_hash_size);

template <size_t N>
    requires is_hash_size<N>
expect<byte_array<N>> read_hash_generic(byte_reader& reader) {
    auto const res = reader.read_bytes(N);
    if ( ! res) {
        return std::unexpected(res.error());
    }
    byte_array<N> arr;
    std::copy(res->begin(), res->end(), arr.begin());
    return arr;
}

inline
expect<hash_digest> read_hash(byte_reader& reader) {
    return read_hash_generic<hash_size>(reader);
}

// TODO: improve all this
template <typename T, typename ... Args>
concept has_from_data = requires(byte_reader& reader, Args&&... args) {
    T::from_data(reader, std::forward<Args>(args)...);
};

// from_data could be:
//      from_data(byte_reader&, bool) and from_data(byte_reader&)
// create a concept for this:
// template <typename T>
// concept has_from_data = requires(byte_reader& reader) {
//     { T::from_data(reader) } -> std::same_as<expect<T>>;
//     { T::from_data(reader, std::ignore) } -> std::same_as<expect<T>>;
// };

template <typename T, typename ... Args>
    requires has_from_data<T, Args...>
expect<std::vector<T>> read_collection(byte_reader& reader, Args&&... args) {
    auto const count_exp = reader.read_size_little_endian();
    if ( ! count_exp) {
        return std::unexpected(count_exp.error());
    }
    auto const count = *count_exp;
    if (count > static_absolute_max_block_size()) {
        return std::unexpected(error::invalid_size);
    }

    std::vector<T> list;
    list.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        auto res = T::from_data(reader, std::forward<Args>(args)...);
        if ( ! res) {
            return std::unexpected(res.error());
        }
        list.emplace_back(std::move(*res));
    }

    return list;
}


} // namespace kth

#endif // KTH_DOMAIN_DESERIALIZATION_HPP
