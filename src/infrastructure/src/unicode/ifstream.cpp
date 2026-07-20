// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/unicode/ifstream.hpp>

#include <fstream>
#include <string>

#include <kth/infrastructure/unicode/unicode.hpp>

namespace kth {

// Construct kth::ifstream.
ifstream::ifstream(std::string const& path, std::ifstream::openmode mode)
#ifdef _MSC_VER
  : std::ifstream(kth::to_utf16(path), mode)
#else
  : std::ifstream(path, mode)
#endif
{
}

} // namespace kth
