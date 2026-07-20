// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/unicode/unicode_ostream.hpp>

#include <cstddef>
#include <iostream>

#include <kth/infrastructure/unicode/unicode_streambuf.hpp>

namespace kth {

unicode_ostream::unicode_ostream(std::ostream& narrow_stream, std::wostream& wide_stream, size_t size)
#ifdef _MSC_VER
    : std::ostream(new unicode_streambuf(wide_stream.rdbuf(), size))
#else
    : std::ostream(narrow_stream.rdbuf())
#endif
{}

#ifdef _MSC_VER
unicode_ostream::~unicode_ostream() {
    delete rdbuf();
}
#endif

} // namespace kth
