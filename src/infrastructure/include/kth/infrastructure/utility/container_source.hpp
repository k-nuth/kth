// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_CONTAINER_SOURCE_HPP
#define KTH_INFRASTRUCTURE_CONTAINER_SOURCE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <boost/iostreams/stream.hpp>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/limits.hpp>

namespace kth {

// modified from boost.iostreams example
// boost.org/doc/libs/1_55_0/libs/iostreams/doc/tutorial/container_source.html
template <typename Container, typename SourceType, typename CharType>
struct KI_API container_source {
    using char_type = CharType;
    using category = boost::iostreams::source_tag;

    explicit
    container_source(const Container& container)
        : container_(container), position_(0) {
        static_assert(sizeof(SourceType) == sizeof(CharType), "invalid size");
    }

    std::streamsize read(char_type* buffer, std::streamsize size) {
        auto const amount = safe_subtract(container_.size(), position_);
        if ( ! amount) {
            return -1;
        }
        auto result = std::min(size, static_cast<std::streamsize>(*amount));

        // TODO: use ios eof symbol (template-based).
        if (result <= 0) {
            return -1;
        }

        auto const value = static_cast<typename Container::size_type>(result);
        std::copy_n(container_.begin() + position_, value, buffer);
        position_ += value;
        return result;
    }

private:
    const Container& container_;
    typename Container::size_type position_;
};

template <typename Container>
using byte_source = container_source<Container, uint8_t, char>;

template <typename Container>
using stream_source = boost::iostreams::stream<byte_source<Container>>;

using data_source = stream_source<data_chunk>;

} // namespace kth

#endif

