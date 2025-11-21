// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UNICODE_STREAMBUF_HPP
#define KTH_INFRASTRUCTURE_UNICODE_STREAMBUF_HPP

#include <cstddef>
#include <streambuf>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/define.hpp>

namespace kth {

/**
 * Class to translate internal utf8 iostreams to external utf16 iostreams.
 * This uses wide and narrow input and output buffers of 1024 characters/bytes.
 * However because of utf8-utf16 conversion ratios of up to 4:1 the effective
 * wide output buffering may be reduced to as much as 256 characters.
 */
struct KI_API unicode_streambuf : std::streambuf {
public:
    /**
     * Construct unicode stream buffer from a weak reference to a wide buffer.
     * @param[in]  wide_buffer  A wide stream buffer for i/o relay.
     * @param[in]  size         The wide buffer size.
     */
    unicode_streambuf(std::wstreambuf* wide_buffer, size_t size);

    /**
     * Synchronize stream buffer.
     */
    ~unicode_streambuf() override;

protected:
    /**
     * Implement underflow for support of input streams.
     */
    std::streambuf::int_type underflow() override;

    /**
     * Implement overflow for support of output streams.
     * @param[in]  character  A single byte to be explicitly put.
     */
    std::streambuf::int_type overflow(std::streambuf::int_type character) override;

    /**
     * Implement sync for support of output streams.
     */
    int sync() override;

private:
    // The constructed wide buffer size in number of characters.
    size_t wide_size_;

    // The derived narrow buffer size in utf8 (bytes).
    size_t narrow_size_;

    // The dynamically-allocated buffers.
    wchar_t* wide_;
    char* narrow_;

    // The excapsulated wide streambuf.
    std::wstreambuf* wide_buffer_;
};

} // namespace kth

#endif
