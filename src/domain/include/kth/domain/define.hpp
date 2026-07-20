// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_DEFINE_HPP
#define KTH_DOMAIN_DEFINE_HPP

#include <kth/infrastructure/compat.hpp>

// Create kd namespace alias.
namespace kth::domain {}
namespace kd = kth::domain;

// See http://gcc.gnu.org/wiki/Visibility

// Generic helper definitions for shared library support
#if defined _MSC_VER || defined __CYGWIN__
#define KD_HELPER_DLL_IMPORT __declspec(dllimport)
#define KD_HELPER_DLL_EXPORT __declspec(dllexport)
#define KD_HELPER_DLL_LOCAL
#else
#if __GNUC__ >= 4
#define KD_HELPER_DLL_IMPORT __attribute__((visibility("default")))
#define KD_HELPER_DLL_EXPORT __attribute__((visibility("default")))
#define KD_HELPER_DLL_LOCAL __attribute__((visibility("internal")))
#else
#define KD_HELPER_DLL_IMPORT
#define KD_HELPER_DLL_EXPORT
#define KD_HELPER_DLL_LOCAL
#endif
#endif

// Now we use the generic helper definitions above to define KD_API
// and KD_INTERNAL. KD_API is used for the public API symbols. It either DLL
// imports or DLL exports (or does nothing for static build) KD_INTERNAL is
// used for non-api symbols.

#if defined KD_STATIC
#define KD_API
#define KD_INTERNAL
#elif defined KD_DLL
#define KD_API KD_HELPER_DLL_EXPORT
#define KD_INTERNAL KD_HELPER_DLL_LOCAL
#else
#define KD_API KD_HELPER_DLL_IMPORT
#define KD_INTERNAL KD_HELPER_DLL_LOCAL
#endif

// Tag to deprecate functions and methods.
// Gives a compiler warning when they are used.
#if defined _MSC_VER || defined __CYGWIN__
#define KD_DEPRECATED __declspec(deprecated)
#else
#if __GNUC__ >= 4
#define KD_DEPRECATED __attribute__((deprecated))
#else
#define KD_DEPRECATED
#endif
#endif

// Avoid namespace conflict between boost::placeholders and std::placeholders.
#define BOOST_BIND_NO_PLACEHOLDERS

// Define so we can have better visibility of lcov exclusion ranges.
#define LCOV_EXCL_START(text)
#define LCOV_EXCL_STOP()

#endif
