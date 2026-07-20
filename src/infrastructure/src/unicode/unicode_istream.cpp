// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/unicode/unicode_istream.hpp>

#include <cstddef>
#include <iostream>

#include <kth/infrastructure/unicode/unicode_streambuf.hpp>

namespace kth {

unicode_istream::unicode_istream(std::istream& narrow_stream, std::wistream& wide_stream, size_t size)
#ifdef _MSC_VER
  : std::istream(new unicode_streambuf(wide_stream.rdbuf(), size))
#else
  : std::istream(narrow_stream.rdbuf())
#endif
{}

#ifdef _MSC_VER
unicode_istream::~unicode_istream() {
    delete rdbuf();
}
#endif

} // namespace kth
