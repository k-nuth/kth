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
  : std::ofstream()
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
