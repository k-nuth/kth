// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_OFSTREAM_HPP
#define KTH_INFRASTRUCTURE_OFSTREAM_HPP

#include <fstream>
#include <string>

#include <kth/infrastructure/define.hpp>

namespace kth {

/**
 * Use kth::ofstream in place of std::ofstream.
 * This provides utf8 to utf16 path translation for Windows.
 */
class KI_API ofstream
    : public std::ofstream
{
public:
    /**
     * Construct kth::ofstream.
     * @param[in]  path  The utf8 path to the file.
     * @param[in]  mode  The file opening mode.
     */
    explicit
    ofstream(std::string const& path, std::ofstream::openmode mode = std::ofstream::out);
};

} // namespace kth

#endif
