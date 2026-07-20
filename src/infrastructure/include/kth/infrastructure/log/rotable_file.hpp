// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_ROTABLE_FILE_HPP
#define KTH_INFRASTRUCTURE_LOG_ROTABLE_FILE_HPP

#include <boost/filesystem.hpp>
#include <boost/log/sources/record_ostream.hpp>

#include <kth/infrastructure/define.hpp>

namespace kth::log {

using stream = boost::shared_ptr<std::ostream>;
using formatter = boost::log::formatting_ostream::ostream_type;

struct rotable_file {
    boost::filesystem::path original_log;
    boost::filesystem::path archive_directory;
    size_t rotation_size;
    size_t minimum_free_space;
    size_t maximum_archive_size;
    size_t maximum_archive_files;
};

} // namespace kth::log

#endif
