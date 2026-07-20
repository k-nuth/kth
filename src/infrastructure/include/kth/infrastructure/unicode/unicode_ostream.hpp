// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UNICODE_OSTREAM_HPP
#define KTH_INFRASTRUCTURE_UNICODE_OSTREAM_HPP

#include <cstddef>
#include <iostream>

#include <kth/infrastructure/define.hpp>

namespace kth {

/**
 * Class to expose a widening output stream.
 */
class KI_API unicode_ostream
    : public std::ostream
{
public:
    /**
     * Construct instance of a conditionally-widening output stream.
     * @param[in]  narrow_stream  A narrow output stream such as std::cout.
     * @param[in]  wide_stream    A wide output stream such as std::wcout.
     * @param[in]  size           The wide buffer size.
     */
    unicode_ostream(std::ostream& narrow_stream, std::wostream& wide_stream,
        size_t size);

    /**
     * Delete the unicode_streambuf that wraps wide_stream.
     */

#ifdef _MSC_VER
    ~unicode_ostream() override;
#else
    ~unicode_ostream() override = default;
#endif

};

} // namespace kth

#endif
