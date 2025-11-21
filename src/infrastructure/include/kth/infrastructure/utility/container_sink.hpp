// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_CONTAINER_SINK_HPP
#define KTH_INFRASTRUCTURE_CONTAINER_SINK_HPP

#include <algorithm>
#include <cstdint>

#include <boost/iostreams/stream.hpp>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// modified from boost.iostreams example
// boost.org/doc/libs/1_55_0/libs/iostreams/doc/tutorial/container_source.html
template <typename Container, typename SinkType, typename CharType>
struct KI_API container_sink {
    using char_type = CharType;
    using category = boost::iostreams::sink_tag;

    explicit
    container_sink(Container& container)
        : container_(container) {
        static_assert(sizeof(SinkType) == sizeof(CharType), "invalid size");
    }

    std::streamsize write(const char_type* buffer, std::streamsize size) {
        auto const safe_sink = reinterpret_cast<const SinkType*>(buffer);
        container_.insert(container_.end(), safe_sink, safe_sink + size);
        return size;
    }

private:
    Container& container_;
};

template <typename Container>
using byte_sink = container_sink<Container, uint8_t, char>;

using data_sink = boost::iostreams::stream<byte_sink<data_chunk>>;

} // namespace kth

#endif

