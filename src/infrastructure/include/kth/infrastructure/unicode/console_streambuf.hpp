// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_CONSOLE_STREAMBUF_HPP
#define KTH_INFRASTRUCTURE_CONSOLE_STREAMBUF_HPP

#include <cstddef>
#include <streambuf>

#include <kth/infrastructure/define.hpp>

namespace kth {

/**
 * Class to patch Windows stdin keyboard input, file input is not a problem.
 * This class and members are no-ops when called in non-MSVC++ builds.
 */
struct KI_API console_streambuf : std::wstreambuf
{
public:
    /**
     * Initialize stdio to use utf8 translation on Windows.
     * @param[in]  size  The stream buffer size.
     */
    static
    void initialize(size_t size);

protected:
    /**
     * Protected construction, use static initialize method.
     * @param[in]  stream_buffer  The stream buffer to patch.
     * @param[in]  size           The stream buffer size.
     */
    console_streambuf(std::wstreambuf const& stream_buffer, size_t size);

    /**
     * Delete stream buffer.
     */
#ifdef _MSC_VER
    ~console_streambuf() override;
#else
    ~console_streambuf() override = default;
#endif


    /**
     * Implement alternate console read.
     * @param[in]  buffer  Pointer to the buffer to fill with console reads.
     * @param[in]  size    The size of the buffer that may be populated.
     */
    std::streamsize xsgetn(wchar_t* buffer, std::streamsize size) override;

    /**
     * Implement alternate console read.
     */
    std::wstreambuf::int_type underflow() override;

private:
    // The constructed buffer size.
    size_t buffer_size_;

    // The dynamically-allocated buffers.
    wchar_t* buffer_;
};

} // namespace kth

#endif
