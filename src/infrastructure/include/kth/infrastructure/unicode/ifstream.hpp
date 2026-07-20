// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_IFSTREAM_HPP
#define KTH_INFRASTRUCTURE_IFSTREAM_HPP

#include <fstream>
#include <string>

#include <kth/infrastructure/define.hpp>

namespace kth {

/**
 * Use kth::ifstream in place of std::ifstream.
 * This provides utf8 to utf16 path translation for Windows.
 */
class KI_API ifstream
    : public std::ifstream
{
public:
    /**
     * Construct kth::ifstream.
     * @param[in]  path  The utf8 path to the file.
     * @param[in]  mode  The file opening mode.
     */
    explicit
    ifstream(std::string const& path, std::ifstream::openmode mode = std::ifstream::in);
};

} // namespace kth

#endif
