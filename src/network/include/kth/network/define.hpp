// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_DEFINE_HPP
#define KTH_NETWORK_DEFINE_HPP

#include <kth/domain.hpp>

// We use the generic helper definitions to define KN_API
// and KN_INTERNAL. KN_API is used for the public API symbols. It either DLL
// imports or DLL exports (or does nothing for static build) KN_INTERNAL is
// used for non-api symbols.

#if defined KN_STATIC
    #define KN_API
    #define KN_INTERNAL
#elif defined KN_DLL
    #define KN_API      KD_HELPER_DLL_EXPORT
    #define KN_INTERNAL KD_HELPER_DLL_LOCAL
#else
    #define KN_API      KD_HELPER_DLL_IMPORT
    #define KN_INTERNAL KD_HELPER_DLL_LOCAL
#endif

// Log name.
#define LOG_NETWORK "[network] "

// Avoid namespace conflict between boost::placeholders and std::placeholders.
#define BOOST_BIND_NO_PLACEHOLDERS

// Include boost only here, so placeholders exclusion works.
#include <kth/infrastructure/utility/asio_helper.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/thread.hpp>

#endif
