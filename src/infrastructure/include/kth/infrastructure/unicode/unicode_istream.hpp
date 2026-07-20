// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UNICODE_ISTREAM_HPP
#define KTH_INFRASTRUCTURE_UNICODE_ISTREAM_HPP

#include <cstddef>
#include <iostream>

#include <kth/infrastructure/define.hpp>

namespace kth {

/**
 * Class to expose a narrowing input stream.
 * std::wcin must be patched by console_streambuf if used for Windows input.
 */
class KI_API unicode_istream
    : public std::istream
{
public:
    /**
     * Construct instance of a conditionally-narrowing input stream.
     * @param[in]  narrow_stream  A narrow input stream such as std::cin.
     * @param[in]  wide_stream    A wide input stream such as std::wcin.
     * @param[in]  size           The wide buffer size.
     */
    unicode_istream(std::istream& narrow_stream, std::wistream& wide_stream,
        size_t size);

    /**
     * Delete the unicode_streambuf that wraps wide_stream.
     */

#ifdef _MSC_VER
    ~unicode_istream() override;
#else
    ~unicode_istream() override = default;
#endif

};

} // namespace kth

#endif
