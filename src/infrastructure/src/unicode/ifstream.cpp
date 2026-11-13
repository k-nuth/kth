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
  : std::ifstream()
{
#ifdef _MSC_VER
    auto const wide = kth::to_utf16(path);
    if (!wide) {
        setstate(std::ios_base::failbit);
        return;
    }
    open(*wide, mode);
#else
    open(path, mode);
#endif
}

} // namespace kth
