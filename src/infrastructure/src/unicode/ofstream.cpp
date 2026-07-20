// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/unicode/ofstream.hpp>

#include <fstream>
#include <string>

#include <kth/infrastructure/unicode/unicode.hpp>

namespace kth {

// Construct kth::ofstream.
ofstream::ofstream(std::string const& path, std::ofstream::openmode mode)
#ifdef _MSC_VER
  : std::ofstream(kth::to_utf16(path), mode)
#else
  : std::ofstream(path, mode)
#endif
{
}

} // namespace kth
