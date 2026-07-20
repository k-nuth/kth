// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_DEFINE_HPP
#define KTH_INFRASTRUCTURE_DEFINE_HPP

#include <kth/infrastructure/compat.hpp>

// Generic helper definitions for shared library support
#if defined _MSC_VER || defined __CYGWIN__
    #define KI_HELPER_DLL_IMPORT __declspec(dllimport)
    #define KI_HELPER_DLL_EXPORT __declspec(dllexport)
    #define KI_HELPER_DLL_LOCAL
#else
    #if __GNUC__ >= 4
        #define KI_HELPER_DLL_IMPORT __attribute__ ((visibility ("default")))
        #define KI_HELPER_DLL_EXPORT __attribute__ ((visibility ("default")))
        #define KI_HELPER_DLL_LOCAL  __attribute__ ((visibility ("internal")))
    #else
        #define KI_HELPER_DLL_IMPORT
        #define KI_HELPER_DLL_EXPORT
        #define KI_HELPER_DLL_LOCAL
    #endif
#endif

// Now we use the generic helper definitions above to define KI_API
// and KI_INTERNAL. KI_API is used for the public API symbols. It either DLL
// imports or DLL exports (or does nothing for static build) KI_INTERNAL is
// used for non-api symbols.

#if defined KI_STATIC
    #define KI_API
    #define KI_INTERNAL
#elif defined KI_DLL
    #define KI_API      KI_HELPER_DLL_EXPORT
    #define KI_INTERNAL KI_HELPER_DLL_LOCAL
#else
    #define KI_API      KI_HELPER_DLL_IMPORT
    #define KI_INTERNAL KI_HELPER_DLL_LOCAL
#endif

// Tag to deprecate functions and methods.
// Gives a compiler warning when they are used.
#if defined _MSC_VER || defined __CYGWIN__
    #define KI_DEPRECATED __declspec(deprecated)
#else
    #if __GNUC__ >= 4
        #define KI_DEPRECATED __attribute__((deprecated))
    #else
        #define KI_DEPRECATED
    #endif
#endif

// Avoid namespace conflict between boost::placeholders and std::placeholders.
#define BOOST_BIND_NO_PLACEHOLDERS

// Define so we can have better visibility of lcov exclusion ranges.
#define LCOV_EXCL_START(text)
#define LCOV_EXCL_STOP()

#endif
