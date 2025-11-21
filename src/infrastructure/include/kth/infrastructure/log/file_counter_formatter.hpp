/*
 *          Copyright Andrey Semashev 2007 - 2014.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   text_file_backend.cpp
 * \author Andrey Semashev
 * \date   09.06.2009
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 */

// Modification of boost implementation to alter log file rotation naming.

#ifndef KTH_INFRASTRUCTURE_LOG_FILE_COUNTER_FORMATTER_HPP
#define KTH_INFRASTRUCTURE_LOG_FILE_COUNTER_FORMATTER_HPP

// #include <filesystem>
#include <sstream>

#include <boost/filesystem/path.hpp>

#include <kth/infrastructure/define.hpp>

namespace kth::log {

// modified from class extracted from boost/log/sinks/text_file_backend.*pp
//! The functor formats the file counter into the file name
struct KI_API file_counter_formatter {
    using path_string_type = boost::filesystem::path::string_type;

public:
    //! Initializing constructor
    explicit
    file_counter_formatter(unsigned int width);

    //! Copy constructor
    file_counter_formatter(file_counter_formatter const& that);

    //! The function formats the file counter into the file name
    path_string_type operator()(path_string_type const& stem, path_string_type const& extension, unsigned int counter) const;

    BOOST_DELETED_FUNCTION(file_counter_formatter& operator= (file_counter_formatter const&))

private:
    //! File counter width
    std::streamsize width_;
    //! The file counter formatting stream
    mutable std::basic_ostringstream<path_string_type::value_type> stream_;
};

} // namespace kth::log

#endif
